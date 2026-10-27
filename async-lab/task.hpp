#pragma once

#include <coroutine>
#include <optional>
#include <exception>

namespace AsyncLab
{
    template <typename T>
    struct TaskResultBase
    {
        std::optional<T> result_;

        void return_value(T value)
        {
            result_ = std::move(value);
        }

        T&& result() noexcept
        {
            return std::move(result_.value());
        }
    };

    template <>
    struct TaskResultBase<void>
    {
        void return_void() { }
    };

    template <typename T>
    class Task
    {
    public:
        struct promise_type : TaskResultBase<T>
        {
            Task get_return_object() noexcept
            {
                return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            struct FinalAwaiter
            {
                bool await_ready() noexcept { return false; }

                // symmetric transfer
                template <typename Promise>
                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept
                {
                    if (auto continuation_handle = handle.promise().continuation_)
                        return continuation_handle;
                    return std::noop_coroutine();
                }

                void await_resume() noexcept { }
            };

            FinalAwaiter final_suspend() noexcept { return {}; }

            void unhandled_exception() noexcept
            {
                exception_ = std::current_exception();
            }

            void set_continuation(std::coroutine_handle<> continuation) noexcept
            {
                continuation_ = continuation;
            }

            std::exception_ptr exception_ = nullptr;
            std::coroutine_handle<> continuation_ = nullptr;
        };

        using CoroutineHandle_t = std::coroutine_handle<promise_type>;

        explicit Task(CoroutineHandle_t handle) noexcept
            : handle_(handle)
        { }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept
            : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                if (handle_)
                {
                    handle_.destroy();
                }
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        ~Task()
        {
            if (handle_)
            {
                handle_.destroy();
            }
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////////
        // Task can be awaited on using co_await, and the result can be retrieved using co_await or sync_wait
        /////////////////////////////////////////////////////////////////////////////////////////////////////

        bool await_ready() const noexcept
        {
            return false;
        }

        // symmetric transfer
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        {
            handle_.promise().set_continuation(awaiting_coroutine);

            return handle_;
        }

        auto await_resume()
        {
            if (handle_.promise().exception_)
            {
                std::rethrow_exception(handle_.promise().exception_);
            }

            if constexpr (!std::is_void_v<T>)
            {
                return std::move(*handle_.promise().result_);
            }
            else
                return;
        }

        CoroutineHandle_t handle() const noexcept
        {
            return handle_;
        }

    private:
        CoroutineHandle_t handle_;
    };
} // namespace AsyncLab