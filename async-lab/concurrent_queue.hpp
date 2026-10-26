#ifndef CONCURRENT_QUEUE_HPP
#define CONCURRENT_QUEUE_HPP

#include <condition_variable>
#include <optional>
#include <queue>
#include <thread>
#include <expected>

namespace AsyncLab
{
    enum class ConqueueStatus 
    {
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
} // namespace AsyncLab

#endif // CONCURRENT_QUEUE_HPP