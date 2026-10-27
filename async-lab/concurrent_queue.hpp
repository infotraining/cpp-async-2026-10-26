#ifndef CONCURRENT_QUEUE_HPP
#define CONCURRENT_QUEUE_HPP

#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <expected>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace AsyncLab
{
    enum class ConqueueStatus {
        Empty,
        Closed,
        Busy
    };

    template <typename T>
    class ConcurrentQueue
    {
        std::queue<T> q_;
        mutable std::mutex mtx_q_;
        std::condition_variable cv_is_q_not_empty_;
        bool is_closed_ = false;

    public:
        void close()
        {
            {
                std::lock_guard lk(mtx_q_);
                is_closed_ = true;
            }

            cv_is_q_not_empty_.notify_all();
        }

        bool is_closed() const
        {
            std::lock_guard lk(mtx_q_);
            return is_closed_;
        }

        bool push(const T& item)
        {
            {
                std::lock_guard lk(mtx_q_);
                if (is_closed_)
                    return false;

                q_.push(item);
            }

            cv_is_q_not_empty_.notify_one();

            return true;
        }

        std::optional<T> pop()
        {
            std::unique_lock lk(mtx_q_);

            auto not_empty_or_closed = [this] { return !q_.empty() || is_closed_; };

            cv_is_q_not_empty_.wait(lk, not_empty_or_closed);

            if (is_closed_ && q_.empty())
                return std::nullopt;

            T val = q_.front();
            q_.pop();

            return val;
        }

        std::expected<T, ConqueueStatus> try_pop()
        {
            std::unique_lock lk(mtx_q_, std::try_to_lock);

            if (not lk.owns_lock())
                return std::unexpected(ConqueueStatus::Busy);

            if (is_closed_ && q_.empty())
                return std::unexpected(ConqueueStatus::Closed);

            if (q_.empty())
                return std::unexpected(ConqueueStatus::Empty);

            T val = q_.front();
            q_.pop();

            return val;
        }
    };

    class ThreadPool
    {
        using WorkToDo = std::function<void()>;

        ConcurrentQueue<WorkToDo> tasks_;
        std::vector<std::thread> threads_;

    public:
        ThreadPool(size_t size = std::thread::hardware_concurrency())
        {
            for (size_t i = 0; i < size; ++i)
            {
                threads_.emplace_back([this] { run(); });
            }
        }

        ~ThreadPool()
        {
            tasks_.close();

            for (auto& thd : threads_)
                thd.join();
        }

        template <typename TFunction>
        auto submit(TFunction func) -> std::future<decltype(func())>
        {
            using TResult = decltype(func());
            auto pt = std::make_shared<std::packaged_task<TResult()>>(func);
            std::future<TResult> f = pt->get_future();

            tasks_.push([pt] { (*pt)(); });

            return f;
        }

        template <typename TFunction>
        auto async_submit(TFunction func)
        {
            using TResult = std::invoke_result_t<TFunction>;

            struct SubmitAwaiter
            {
                TFunction func_;
                ThreadPool& thd_pool_;
                std::optional<TResult> result_;

                SubmitAwaiter(TFunction func, ThreadPool& thd_pool)
                    : func_(std::move(func)), thd_pool_(thd_pool) {}

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> awaiting_coro)
                {
                    auto task_wrapper = [this, awaiting_coro] {
                        result_ = func_();
                        awaiting_coro.resume();
                    };

                    [[maybe_unused]] auto res = thd_pool_.tasks_.push(std::move(task_wrapper));
                    assert(res);
                }

                decltype(auto) await_resume() { return *result_; }
            };

            return SubmitAwaiter{std::move(func), *this};
        }


    private:
        void run()
        {
            while (true)
            {
                std::optional<WorkToDo> task = tasks_.pop();

                if (not task)
                    return;

                (*task)(); // execution of task in worker thread
            }
        }
    };
} // namespace AsyncLab

#endif // CONCURRENT_QUEUE_HPP