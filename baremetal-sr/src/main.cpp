// main.cpp
// Minimal simulation of Intel Baremetal Senders/Receivers concepts on Linux.
// - periodic scheduler (timer thread)
// - simple pipeline with let_value semantics
// - when_all style composition
// - incite_on style stage migration (using worker threads)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

using namespace std::chrono_literals;

// -----------------------------
// Utilities: simple thread-safe queue for incite_on
// -----------------------------
template <typename T>
class SPSCQueue
{
    // single-producer single-consumer ring buffer
    std::vector<T> buf;
    std::atomic<size_t> head{0}, tail{0};
    size_t capacity;

public:
    explicit SPSCQueue(size_t cap = 1024)
        : buf(cap)
        , capacity(cap)
    { }
    bool try_push(const T& v)
    {
        size_t h = head.load(std::memory_order_relaxed);
        size_t n = (h + 1) % capacity;
        if (n == tail.load(std::memory_order_acquire))
            return false; // full
        buf[h] = v;
        head.store(n, std::memory_order_release);
        return true;
    }
    bool try_pop(T& out)
    {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire))
            return false; // empty
        out = buf[t];
        tail.store((t + 1) % capacity, std::memory_order_release);
        return true;
    }
};

// -----------------------------
// TriggerScheduler: simulate interrupt-driven scheduler
// - trigger() wakes the scheduler
// - register a callback to be invoked when triggered
// -----------------------------
class TriggerScheduler
{
    std::mutex m;
    std::condition_variable cv;
    std::function<void()> callback;
    std::atomic<bool> running{true};
    std::thread worker;

public:
    TriggerScheduler()
    {
        // worker waits for trigger() to call callback
        worker = std::thread([this]() {
            std::unique_lock<std::mutex> lk(m);
            while (running.load())
            {
                cv.wait(lk);
                if (!running.load())
                    break;
                // call callback outside lock to avoid deadlocks
                auto cb = callback;
                lk.unlock();
                if (cb)
                    cb();
                lk.lock();
            }
        });
    }
    ~TriggerScheduler()
    {
        running.store(false);
        cv.notify_all();
        if (worker.joinable())
            worker.join();
    }
    void set_callback(std::function<void()> cb)
    {
        std::lock_guard<std::mutex> lk(m);
        callback = std::move(cb);
    }
    void trigger()
    {
        // simulate ISR calling trigger
        std::lock_guard<std::mutex> lk(m);
        cv.notify_one();
    }
};

// -----------------------------
// Simulated hardware and helpers
// -----------------------------
int simulated_adc_read(int sensor_id)
{
    // deterministic pseudo-sensor
    static std::atomic<int> counter{0};
    int base = ++counter;
    return base * (sensor_id + 1);
}

int simple_lowpass(int x)
{
    // trivial processing
    return x / 2;
}

void simulated_uart_send(int v)
{
    // simulate a short non-blocking send
    std::cout << "[UART] sent: " << v << "\n";
}

// -----------------------------
// Part A: Simple pipeline
// - periodic scheduler -> read ADC -> filter -> UART
// -----------------------------
void part_simple(TriggerScheduler& timer_sched)
{
    // register callback to run pipeline on each trigger
    timer_sched.set_callback([&]() {
        int raw = simulated_adc_read(0);
        int processed = simple_lowpass(raw);
        simulated_uart_send(processed);
    });
}

// -----------------------------
// Part B: when_all style composition
// - two sensors sampled on same tick -> fuse -> send
// Implementation approach:
// - On each tick, read both sensors synchronously and then fuse.
// - This simulates when_all semantics for same scheduler tick.
// -----------------------------
void part_when_all(TriggerScheduler& timer_sched)
{
    timer_sched.set_callback([&]() {
        // read both sensors
        auto r1 = simulated_adc_read(1);
        auto r2 = simulated_adc_read(2);
        // fuse (simple sum)
        int fused = r1 + r2;
        simulated_uart_send(fused);
    });
}

// -----------------------------
// Part C: incite_on style stage migration
// - timer_sched triggers ADC read (fast)
// - processing moved to proc_worker (lower priority thread)
// - UART send moved to uart_worker (lowest priority thread)
// Implementation approach:
// - incite_on enqueues work into a queue for the target worker and triggers it.
// -----------------------------
struct WorkItem
{
    int value;
};

void part_incite_on(TriggerScheduler& timer_sched,
    SPSCQueue<WorkItem>& proc_queue,
    SPSCQueue<WorkItem>& uart_queue,
    std::function<void()> proc_trigger,
    std::function<void()> uart_trigger)
{
    // Timer callback: read ADC and enqueue to processing queue
    timer_sched.set_callback([&]() {
        int raw = simulated_adc_read(3);
        WorkItem w{raw};
        if (!proc_queue.try_push(w))
        {
            std::cout << "[INCITE] proc queue full, dropping sample\n";
            return;
        }
        proc_trigger(); // notify processing worker
    });

    // processing worker thread will pop from proc_queue, process, and enqueue to uart_queue
    // uart worker will pop and send
}

// -----------------------------
// Worker thread functions
// -----------------------------
void processing_worker(SPSCQueue<WorkItem>& proc_queue,
    SPSCQueue<WorkItem>& uart_queue,
    std::atomic<bool>& run_flag,
    std::condition_variable& proc_cv,
    std::mutex& proc_m)
{
    while (run_flag.load())
    {
        // wait for notification
        std::unique_lock<std::mutex> lk(proc_m);
        proc_cv.wait_for(lk, 10ms);
        // drain queue
        WorkItem w;
        while (proc_queue.try_pop(w))
        {
            int processed = simple_lowpass(w.value);
            WorkItem out{processed};
            if (!uart_queue.try_push(out))
            {
                std::cout << "[INCITE] uart queue full, dropping processed\n";
            }
        }
    }
}

void uart_worker(SPSCQueue<WorkItem>& uart_queue,
    std::atomic<bool>& run_flag,
    std::condition_variable& uart_cv,
    std::mutex& uart_m)
{
    while (run_flag.load())
    {
        std::unique_lock<std::mutex> lk(uart_m);
        uart_cv.wait_for(lk, 10ms);
        WorkItem w;
        while (uart_queue.try_pop(w))
        {
            simulated_uart_send(w.value);
        }
    }
}

// -----------------------------
// Main: wire everything and run for a short demo
// -----------------------------
int main()
{
    std::cout << "Baremetal S/R simulation on Linux (C++20)\n";

    // Timer scheduler (simulates high-priority timer ISR)
    TriggerScheduler timer_sched;

    // Part A demo: simple pipeline
    std::cout << "\n--- Part A: simple pipeline demo (3 ticks) ---\n";
    part_simple(timer_sched);
    for (int i = 0; i < 3; ++i)
    {
        timer_sched.trigger();
        std::this_thread::sleep_for(50ms);
    }

    // Part B demo: when_all style
    std::cout << "\n--- Part B: when_all demo (3 ticks) ---\n";
    part_when_all(timer_sched);
    for (int i = 0; i < 3; ++i)
    {
        timer_sched.trigger();
        std::this_thread::sleep_for(50ms);
    }

    // Part C demo: incite_on style with worker threads
    std::cout << "\n--- Part C: incite_on demo (3 ticks) ---\n";
    SPSCQueue<WorkItem> proc_queue(16);
    SPSCQueue<WorkItem> uart_queue(16);

    std::atomic<bool> run_flag{true};
    std::condition_variable proc_cv, uart_cv;
    std::mutex proc_m, uart_m;

    // processing worker
    std::thread proc_thread([&]() {
        processing_worker(proc_queue, uart_queue, run_flag, proc_cv, proc_m);
    });

    // uart worker
    std::thread uart_thread([&]() {
        uart_worker(uart_queue, run_flag, uart_cv, uart_m);
    });

    // triggers for workers (incite_on simulation)
    auto proc_trigger = [&]() {
        proc_cv.notify_one();
    };
    
    auto uart_trigger = [&]() {
        uart_cv.notify_one();
    };

    // install incite_on pipeline
    part_incite_on(timer_sched, proc_queue, uart_queue, proc_trigger, uart_trigger);

    // fire a few ticks
    for (int i = 0; i < 3; ++i)
    {
        timer_sched.trigger();
        // give time for workers to process
        std::this_thread::sleep_for(100ms);
        // notify uart worker in case processing enqueued items
        uart_cv.notify_one();
    }

    // shutdown workers
    run_flag.store(false);
    proc_cv.notify_one();
    uart_cv.notify_one();
    if (proc_thread.joinable())
        proc_thread.join();
    if (uart_thread.joinable())
        uart_thread.join();

    std::cout << "Demo complete.\n";
    return 0;
}
