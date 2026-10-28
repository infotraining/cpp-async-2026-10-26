// main_intel.cpp
// The baremetal_sr demo rewritten with Intel's cpp-baremetal-senders-and-receivers.
//
// Interrupts are simulated on Linux:
//  - timer interrupts   -> timerfd + a thread acting as the ISR
//  - software interrupts -> eventfd + a thread acting as the ISR
// Every ISR just calls async::run_triggers<"name">(...), exactly as a real ISR would.

#include <async/incite_on.hpp>
#include <async/just.hpp>
#include <async/just_result_of.hpp>
#include <async/let_value.hpp>
#include <async/read_env.hpp>
#include <async/repeat.hpp>
#include <async/schedulers/trigger_scheduler.hpp>
#include <async/start_detached.hpp>
#include <async/stop_token.hpp>
#include <async/sync_wait.hpp>
#include <async/then.hpp>
#include <async/variant_sender.hpp>
#include <async/when_any.hpp>
#include <async/when_all.hpp>

#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

// -----------------------------
// Simulated interrupt sources
// -----------------------------
namespace {
[[noreturn]] void fail(const char* what)
{
    throw std::runtime_error(what);
}

std::mutex g_cout_mutex;

template <typename... Ts>
void log(Ts&&... xs)
{
    std::lock_guard lk{g_cout_mutex};
    (std::cout << ... << xs) << std::endl;
}

// Periodic hardware timer: timerfd expiration -> ISR thread -> handler()
class TimerIrq
{
    int timer_fd_;
    int stop_fd_;
    std::function<void()> isr_;
    std::thread thread_;

public:
    TimerIrq(std::chrono::nanoseconds first, std::chrono::nanoseconds period, std::function<void()> isr)
        : timer_fd_(::timerfd_create(CLOCK_MONOTONIC, 0))
        , stop_fd_(::eventfd(0, 0))
        , isr_(std::move(isr))
    {
        // Set up the timerfd with the initial expiration and period.
        if (timer_fd_ < 0 || stop_fd_ < 0)
            fail("timerfd/eventfd creation failed");

        // Convert the first and period durations to timespec for timerfd.
        auto to_ts = [](std::chrono::nanoseconds d) {
            return timespec{static_cast<time_t>(d.count() / 1'000'000'000), static_cast<long>(d.count() % 1'000'000'000)};
        };

        // Set up the itimerspec structure with the converted timespec values.
        itimerspec spec{to_ts(period), to_ts(first)};
        
        // The itimerspec structure is now ready to be used with timerfd_settime.
        if (::timerfd_settime(timer_fd_, 0, &spec, nullptr) != 0)
            fail("timerfd_settime failed");

        thread_ = std::thread([this] {
            // Poll for timer expirations or stop events.
            pollfd fds[2] = {{timer_fd_, POLLIN, 0}, {stop_fd_, POLLIN, 0}};
            while (::poll(fds, 2, -1) >= 0) // Continue polling until an error occurs or stop event is triggered.
            {
                if (fds[1].revents & POLLIN) // Stop event triggered, exit the polling loop.
                    return;
                if (fds[0].revents & POLLIN) // Timer event triggered, call the ISR.
                {
                    std::uint64_t expirations{};
                    if (::read(timer_fd_, &expirations, sizeof expirations) == sizeof expirations)
                        isr_();
                }
            }
        });
    }

    TimerIrq(const TimerIrq&) = delete;
    TimerIrq& operator=(const TimerIrq&) = delete;

    ~TimerIrq()
    {
        std::uint64_t one = 1;
        [[maybe_unused]] auto r = ::write(stop_fd_, &one, sizeof one);
        thread_.join();
        ::close(timer_fd_);
        ::close(stop_fd_);
    }
};

// Software-triggered interrupt carrying a payload (like a pended lower-priority IRQ).
// raise() may be called from any context; the ISR runs on its own thread.
template <typename T>
class SoftIrq
{
    int event_fd_;
    int stop_fd_;
    std::function<void(T)> isr_;
    std::mutex m_;
    std::deque<T> pending_;
    std::thread thread_;

public:
    explicit SoftIrq(std::function<void(T)> isr)
        : event_fd_(::eventfd(0, EFD_SEMAPHORE))
        , stop_fd_(::eventfd(0, 0))
        , isr_(std::move(isr))
    {
        if (event_fd_ < 0 || stop_fd_ < 0)
            fail("eventfd creation failed");

        thread_ = std::thread([this] {
            pollfd fds[2] = {{event_fd_, POLLIN, 0}, {stop_fd_, POLLIN, 0}};
            while (::poll(fds, 2, -1) >= 0)
            {
                if (fds[1].revents & POLLIN)
                    return;
                if (fds[0].revents & POLLIN)
                {
                    std::uint64_t n{};
                    if (::read(event_fd_, &n, sizeof n) != sizeof n)
                        continue;
                    T value;
                    {
                        std::lock_guard lk{m_};
                        value = pending_.front();
                        pending_.pop_front();
                    }
                    isr_(value);
                }
            }
        });
    }

    SoftIrq(const SoftIrq&) = delete;
    SoftIrq& operator=(const SoftIrq&) = delete;

    ~SoftIrq()
    {
        std::uint64_t one = 1;
        [[maybe_unused]] auto r = ::write(stop_fd_, &one, sizeof one);
        thread_.join();
        ::close(event_fd_);
        ::close(stop_fd_);
    }

    void raise(T value)
    {
        {
            std::lock_guard lk{m_};
            pending_.push_back(value);
        }
        std::uint64_t one = 1;
        [[maybe_unused]] auto r = ::write(event_fd_, &one, sizeof one);
    }
};

// -----------------------------
// Simulated hardware helpers
// -----------------------------
int simulated_adc_read(int sensor_id)
{
    static std::atomic<int> counter{0};
    return ++counter * (sensor_id + 1);
}

int simple_lowpass(int x)
{
    return x / 2;
}

void simulated_uart_send(int v)
{
    log("[UART] sent: ", v);
}

constexpr int ticks = 3;

template <typename S>
void start(S&& sndr)
{
    [[maybe_unused]] auto started = std::forward<S>(sndr) | async::start_detached();
}

// Blocks the "main" context until the detached pipeline signals completion.
class Done
{
    std::atomic<bool> flag_{false};

public:
    void set()
    {
        flag_.store(true);
        flag_.notify_all();
    }
    void wait() { flag_.wait(false); }
};
} // namespace

// -----------------------------
// Part A: simple pipeline
// timer ISR -> read ADC -> filter -> UART, repeated for `ticks` ticks
// -----------------------------
void part_simple()
{
    Done done;
    int count = 0;

    TimerIrq timer_isr{50ms, 50ms, [] { async::run_triggers<"timer_a">(); }};

    start(async::trigger_scheduler<"timer_a">{}.schedule()
        | async::then([] { return simulated_adc_read(0); })
        | async::then([](int x) { return simple_lowpass(x); })
        | async::then([](int v) { simulated_uart_send(v); })
        | async::repeat_until([&](auto&&...) { return ++count == ticks; })
        | async::then([&] { done.set(); })
);

    done.wait();
}

// -----------------------------
// Part B: when_all
// Two ADCs complete their conversions on separate interrupts (offset by 10ms);
// when_all joins both results, and they are fused and sent.
// -----------------------------
void part_when_all()
{
    Done done;
    int count = 0;

    TimerIrq adc1_isr{50ms, 50ms, [] { async::run_triggers<"adc1_done">(); }};
    TimerIrq adc2_isr{60ms, 50ms, [] { async::run_triggers<"adc2_done">(); }};

    auto adc1 = async::trigger_scheduler<"adc1_done">{}.schedule() | async::then([] { return simulated_adc_read(1); });
    auto adc2 = async::trigger_scheduler<"adc2_done">{}.schedule() | async::then([] { return simulated_adc_read(2); });

    start(async::when_all(adc1, adc2)
        | async::then([](int r1, int r2) { return r1 + r2; })
        | async::then([](int v) { simulated_uart_send(v); })
        | async::repeat_until([&](auto&&...) { return ++count == ticks; })
        | async::then([&] { done.set(); })
);

    done.wait();
}

// -----------------------------
// Part C: incite_on
// timer ISR: read ADC, then incite the "proc" ISR (lower priority);
// the "proc" ISR filters, then incites the "uart" ISR which sends.
// Each stage runs in a different interrupt context (thread).
// -----------------------------
void part_incite_on()
{
    Done done;
    int count = 0;

    SoftIrq<int> uart_isr{[](int v) { async::run_triggers<"uart_irq">(v); }};
    SoftIrq<int> proc_isr{[](int v) { async::run_triggers<"proc_irq">(v); }};
    TimerIrq timer_isr{50ms, 50ms, [] { async::run_triggers<"timer_c">(); }};

    start(async::trigger_scheduler<"timer_c">{}.schedule()
        | async::then([] { return simulated_adc_read(3); })
        | async::then([&](int raw) { return [&, raw] { proc_isr.raise(raw); }; })
        | async::incite_on(async::trigger_scheduler<"proc_irq", int>{})
        | async::then([](int x) { return simple_lowpass(x); })
        | async::then([&](int processed) { return [&, processed] { uart_isr.raise(processed); }; })
        | async::incite_on(async::trigger_scheduler<"uart_irq", int>{})
        | async::then([](int v) { simulated_uart_send(v); })
        | async::repeat_until([&](auto&&...) { return ++count == ticks; })
        | async::then([&] { done.set(); })
);

    done.wait();
}

// -----------------------------
// Part D: error handling
// Every other ADC conversion is faulty. let_value validates the sample and
// chooses between just(value) and just_error(code) at run time. The error
// skips the downstream `then` and is handled by upon_error, which turns it back
// into a value completion so that repeat_until keeps the pipeline running.
// -----------------------------
enum class adc_error { out_of_range };

void part_errors()
{
    Done done;
    int count = 0;

    TimerIrq timer_isr{50ms, 50ms, [] { async::run_triggers<"timer_d">(); }};

    start(async::trigger_scheduler<"timer_d">{}.schedule()
        | async::then([] {
              static int n = 0;
              return (++n % 2 == 0) ? 1000 : simulated_adc_read(4);
          })
        | async::let_value([](int raw) {
              return async::make_variant_sender(
                  raw < 100,
                  [raw] { return async::just(raw); },
                  [] { return async::just_error(adc_error::out_of_range); });
          })
        | async::then([](int x) { simulated_uart_send(simple_lowpass(x)); })
        | async::upon_error([](adc_error) { log("[ERROR] ADC sample out of range, dropped"); })
        | async::repeat_until([&](auto&&...) { return ++count == ticks + 1; })
        | async::then([&] { done.set(); }));

    done.wait();
}

// -----------------------------
// Part E: cancellation with a stop token
// An endless pipeline is raced against a "cancel" trigger using stop_when. The
// cancel interrupt (a one-shot timer, think "button press") completes the
// trigger sender; stop_when then requests stop on the pipeline's stop token,
// which dequeues the pending timer trigger operation and ends `repeat`.
//
// Note: calling request_stop() on the handle of start_detached_stoppable from
// the thread that then waits would deadlock in this Linux setup: the library
// completes the pipeline inline while holding its (non-recursive std::mutex)
// concurrency policy lock, and the completion takes the same lock again.
// -----------------------------
void part_stop()
{
    Done done;
    std::atomic<int> samples{0};

    TimerIrq timer_isr{50ms, 50ms, [] { async::run_triggers<"timer_e">(); }};
    TimerIrq cancel_isr{175ms, 0ns, [] { async::run_triggers<"cancel_e">(); }};

    auto pipeline = async::trigger_scheduler<"timer_e">{}.schedule()
        | async::then([] { return simulated_adc_read(5); })
        | async::then([&](int x) {
              ++samples;
              simulated_uart_send(simple_lowpass(x));
          })
        | async::repeat();

    auto cancel = async::trigger_scheduler<"cancel_e">{}.schedule()
        | async::then([&] { log("[STOP] cancel requested after ", samples.load(), " samples"); });

    start(std::move(pipeline)
        | async::stop_when(std::move(cancel))
        | async::then([&] { done.set(); }));

    done.wait();
}

// -----------------------------
// Part F: environment queries
// An environment is a set of key/value pairs (queries) attached to the
// receiver at the end of a pipeline. Every adaptor forwards it upstream, so any
// sender can read it with read_env(query). Here the environment carries
//  - the standard get_stop_token query (an inplace_stop_source owned by main),
//  - a custom get_gain query (a calibration factor for the ADC).
// The pipeline reads both without them being passed as arguments. The cancel
// ISR requests stop on the source; the pending trigger operation observes the
// same token through the environment and completes as stopped, which makes
// sync_wait return an empty optional.
// -----------------------------
struct get_gain_t : async::forwarding_query_t
{
    constexpr static auto name = stdx::ct_string{"get_gain"};

    template <typename E>
    constexpr auto operator()(E&& e) const -> decltype(std::forward<E>(e).query(*this))
    {
        return std::forward<E>(e).query(*this);
    }
};
constexpr inline get_gain_t get_gain{};

void part_env()
{
    async::inplace_stop_source stop_src;
    auto env = async::env{
        async::prop{async::get_stop_token_t{}, stop_src.get_token()},
        async::prop{get_gain_t{}, 3}};

    TimerIrq timer_isr{50ms, 50ms, [] { async::run_triggers<"timer_f">(); }};
    TimerIrq cancel_isr{175ms, 0ns, [&] { stop_src.request_stop(); }};

    auto sndr = async::trigger_scheduler<"timer_f">{}.schedule()
        | async::then([] { return simulated_adc_read(6); })
        | async::let_value([](int raw) {
              return async::read_env(get_gain, async::get_stop_token)
                  | async::then([raw](int gain, auto token) {
                        log("[ENV] gain=", gain, " stop_possible=", token.stop_possible(),
                            " stop_requested=", token.stop_requested());
                        simulated_uart_send(raw * gain);
                    });
          })
        | async::repeat()
        | async::upon_stopped([] { log("[ENV] pipeline stopped via the environment's stop token"); });

    // sync_wait needs a value completion: upon_stopped maps "stopped" to one
    [[maybe_unused]] auto result = async::sync_wait_dynamic(std::move(sndr), std::move(env));
}

int main()
{
    std::cout << "Intel baremetal S/R on Linux (simulated interrupts)\n";

    std::cout << "\n--- Part A: simple pipeline (" << ticks << " ticks) ---\n";
    part_simple();

    std::cout << "\n--- Part B: when_all (" << ticks << " ticks) ---\n";
    part_when_all();

    std::cout << "\n--- Part C: incite_on (" << ticks << " ticks) ---\n";
    part_incite_on();

    std::cout << "\n--- Part D: error handling ---\n";
    part_errors();

    std::cout << "\n--- Part E: stop token ---\n";
    part_stop();

    std::cout << "\n--- Part F: environment queries ---\n";
    part_env();

    std::cout << "Demo complete.\n";
}
