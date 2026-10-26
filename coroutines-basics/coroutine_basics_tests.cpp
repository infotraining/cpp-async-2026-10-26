#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <coroutine>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

class TaskResumer
{ 
public:
	struct promise_type
	{
		TaskResumer get_return_object()
		{
			return TaskResumer(std::coroutine_handle<promise_type>::from_promise(*this));
		}

		std::suspend_always initial_suspend() { return {}; }

		std::suspend_always final_suspend() noexcept { return {}; }

		void return_void() {}

		void unhandled_exception()
		{
			std::terminate();
		}
	};
	explicit TaskResumer(std::coroutine_handle<promise_type> h)
		: handle_{h}
	{}

	TaskResumer(const TaskResumer&) = delete;
	TaskResumer& operator=(const TaskResumer&) = delete;

	~TaskResumer()
	{
		if (handle_)
			handle_.destroy();
	}

	bool resume()
	{
		if (!handle_ || handle_.done())
			return false;

		handle_.resume(); // or handle_();
		return true;
	}


private:
	std::coroutine_handle<promise_type> handle_;
};

TaskResumer simple_coroutine(std::string name)
{
    std::cout << "Starting " << name << "..." << std::endl;

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed..." << std::endl;

    co_await std::suspend_always(); // suspension point

    std::cout << name << " has been resumed again..." << std::endl;
    std::cout << name << " is finishing..." << std::endl;

    co_return; // end of the coroutine
}

TEST_CASE("simple coroutine")
{
    TaskResumer coro_task = simple_coroutine("Coro#1");

    std::cout << "Coroutine created..." << std::endl;

    do
    {
        std::cout << "Resuming coroutine from main..." << std::endl;
    } while (coro_task.resume());

    std::cout << "Coroutine has finished execution." << std::endl;

    REQUIRE(true);
}
