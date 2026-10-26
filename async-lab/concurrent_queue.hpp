#ifndef CONCURRENT_QUEUE_HPP
#define CONCURRENT_QUEUE_HPP

#include <condition_variable>
#include <optional>
#include <queue>
#include <thread>

namespace AsyncLab
{
    enum class ConqueueStatus 
    {
        Success,
        Empty,
        Closed,
        Busy
    };

    template <typename T>
    class ConcurrentQueue
    {
    };
} // namespace AsyncLab

#endif // CONCURRENT_QUEUE_HPP