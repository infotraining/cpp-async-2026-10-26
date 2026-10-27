#pragma once

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <system_error>

// Base class of everything registered in the reactor. When epoll reports
// readiness for the fd, complete() is called on the reactor thread.
struct IoOperation
{
    virtual void complete() = 0;

protected:
    ~IoOperation() = default;
};

// Single-threaded reactor based on epoll.
class IoContext
{
public:
    IoContext()
        : epoll_fd_{::epoll_create1(EPOLL_CLOEXEC)}
    {
        if (epoll_fd_ < 0)
            throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }

    IoContext(const IoContext&) = delete;
    IoContext& operator=(const IoContext&) = delete;

    ~IoContext() { ::close(epoll_fd_); }

    void add(int fd, IoOperation* op, uint32_t events = EPOLLIN)
    {
        epoll_event ev{}; 
        ev.events = events | EPOLLONESHOT; // Ensure the event is one-shot, must be re-armed after each readiness.
        ev.data.ptr = op;  // Store the pointer to the IoOperation so it can be retrieved in epoll_wait.
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0)
            throw std::system_error(errno, std::generic_category(), "epoll_ctl(ADD)");
        ++pending_;
    }

    // Runs until there are no more registered operations.
    void run()
    {
        std::array<epoll_event, 64> events; // Buffer to store events returned by epoll_wait.
        while (pending_ > 0) // Continue running as long as there are pending operations.
        {
            int n = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), -1);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                throw std::system_error(errno, std::generic_category(), "epoll_wait");
            }
            for (int i = 0; i < n; ++i)
            {
                --pending_;
                static_cast<IoOperation*>(events[i].data.ptr)->complete();
            }
        }
    }

private:
    int epoll_fd_;
    std::size_t pending_ = 0;
};

// co_await delay(ctx, 100ms);
class DelayAwaiter : IoOperation
{
public:
    DelayAwaiter(IoContext& ctx, std::chrono::nanoseconds d)
        : ctx_{ctx}
        , duration_{d}
    {
    }

    DelayAwaiter(const DelayAwaiter&) = delete;
    DelayAwaiter& operator=(const DelayAwaiter&) = delete;

    ~DelayAwaiter()
    {
        if (timer_fd_ >= 0)
            ::close(timer_fd_);
    }

    bool await_ready() const noexcept { return duration_ <= std::chrono::nanoseconds::zero(); }

    void await_suspend(std::coroutine_handle<> h)
    {
        // Create a timerfd to implement the delay.
        timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (timer_fd_ < 0)
            throw std::system_error(errno, std::generic_category(), "timerfd_create");

        itimerspec spec{}; // Initialize the timer specification structure to zero.
        spec.it_value.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration_).count();
        spec.it_value.tv_nsec = (duration_ % std::chrono::seconds(1)).count();
        if (::timerfd_settime(timer_fd_, 0, &spec, nullptr) < 0)
            throw std::system_error(errno, std::generic_category(), "timerfd_settime");

        handle_ = h; //
        ctx_.add(timer_fd_, this); // Register the timerfd with the IoContext to be monitored for readiness.
    }

    void await_resume() const noexcept {}

private:
    void complete() override
    {
        std::uint64_t expirations; 
        [[maybe_unused]] auto r = ::read(timer_fd_, &expirations, sizeof(expirations)); // Read and clear the timerfd.
        handle_.resume(); // Resume the coroutine that was suspended on this delay.
    }

    IoContext& ctx_;
    std::chrono::nanoseconds duration_;
    int timer_fd_ = -1;
    std::coroutine_handle<> handle_;
};

inline DelayAwaiter delay(IoContext& ctx, std::chrono::nanoseconds d)
{
    return DelayAwaiter{ctx, d};
}

// Eager, fire-and-forget coroutine; its frame is destroyed on completion.
struct Detached
{
    struct promise_type
    {
        Detached get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
};
