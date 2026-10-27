#include <coroutine>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <thread>

class DetachedTask
{
public:
    struct promise_type
    {   
        std::atomic<bool> done_{false};

        DetachedTask get_return_object() { 
            return DetachedTask{std::coroutine_handle<promise_type>::from_promise(*this)}; }

        std::suspend_never initial_suspend() { return {}; }
        
        std::suspend_always final_suspend() noexcept 
        {
            std::cout << "Final suspend on thread#" << std::this_thread::get_id() << std::endl;
            done_.store(true);
            done_.notify_all();
            return {}; 
        }

        void return_void() {}
        
        void unhandled_exception() { std::terminate(); }
    };

    void wait() 
    {
        handle_.promise().done_.wait(false);

        std::cout << "Done: " << handle_.done() << std::endl;
    }
    explicit DetachedTask(std::coroutine_handle<promise_type> handle) : handle_(handle) {} 
    
    ~DetachedTask() 
    {
        if (handle_)
            handle_.destroy();
    }

    std::coroutine_handle<promise_type> handle_;
};

auto resume_on_new_thread()
{
    struct ThreadResumerAwaiter
    {
        bool await_ready() const noexcept 
        {
            std::cout << "Awaiting on thread#" << std::this_thread::get_id() << std::endl; 
            return false; 
        }

        void await_suspend(std::coroutine_handle<> awaiting_coro) const
        {
            std::thread::id current_thread_id = std::this_thread::get_id();
            std::cout << "Creating new thread from thread#" << current_thread_id << std::endl; 
            std::thread([awaiting_coro, current_thread_id]() mutable {
                int x = 42;
                std::cout << "Starting new thread from thread#" << current_thread_id << std::endl; 
                awaiting_coro.resume();
            }).detach();
        }

        std::thread::id await_resume() const noexcept 
        {
            std::thread::id new_thread_id = std::this_thread::get_id();
            std::cout << "Resumed on thread#" << new_thread_id << std::endl; 
            return new_thread_id;
        }
    };

    return ThreadResumerAwaiter{};
}

DetachedTask coro_on_many_threads(std::string name)
{
    int counter = 0;

    std::cout << "Starting " << name 
        << " on thread#" << std::this_thread::get_id() << std::endl; 
        
    std::cout << "Counter before awaiting: " << counter << std::endl;

    std::thread::id new_thread_id = co_await resume_on_new_thread();
    counter++;
    
    std::cout << "Continue " << name 
        << " on thread#" << new_thread_id << std::endl; 
    std::cout << "Counter after awaiting: " << counter << std::endl;

    new_thread_id = co_await resume_on_new_thread();
    counter++;
    
    std::cout << "Continue " << name 
        << " on thread#" << new_thread_id << std::endl; 
    std::cout << "Counter after awaiting: " << counter << std::endl;
}

TEST_CASE("basic awaiter")
{
    DetachedTask dtask = coro_on_many_threads("coro#1");
    
    dtask.wait();
}   
