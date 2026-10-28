#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <random>
#include <thread>
#include <expected>
#include <syncstream>

#include <boost/capy.hpp>

std::osyncstream synced_cout()
{
    return std::osyncstream(std::cout);
}

namespace capy = boost::capy;

using namespace std::literals;

capy::task<int> simple_task(int value = 42)
{
	if (value == 13)
		throw std::runtime_error("Error#13");

	co_return value;
}

capy::task<int> nested_tasks()
{
	int result = co_await simple_task();

	co_return 2 * result;
}

TEST_CASE("capy - simple task")
{
	capy::thread_pool thd_pool(8);
	capy::Executor auto ex = thd_pool.get_executor();
	
	int value{};
	
	auto on_complete = [&value](int result) { 
		std::cout << "Task completed with " << result << "\n";
		value = result;
	};

	capy::run_async(ex, on_complete)(nested_tasks());
	
	thd_pool.join();
	
	std::cout << "Value: " << value << "\n";
}

TEST_CASE("capy - error handling")
{
	capy::thread_pool thd_pool(8);
	capy::Executor auto ex = thd_pool.get_executor();

	auto on_complete = [](int result) { 
		std::cout << "Task completed with " << result << "\n";
	};

	auto on_error = [](std::exception_ptr eptr) {
		try
		{
			if (eptr)
				std::rethrow_exception(eptr);
		}
		catch(const std::runtime_error& e)
		{
			std::cout << "Exception: " << e.what() << "\n";
		}
	};

	capy::run_async(ex, on_complete, on_error)(simple_task(13));

	thd_pool.join();
}

capy::task<std::expected<int, std::tuple<std::error_code, int>>> long_running_task()
{
	std::stop_token stp_tkn = co_await capy::this_coro::stop_token;

	for(int i = 0; i < 100; ++i)
	{
		std::cout << "Iteration#" << i << std::endl;

		std::this_thread::sleep_for(10ms);

		if (stp_tkn.stop_requested())
		{
			co_return std::unexpected(std::tuple{std::make_error_code(std::errc::operation_canceled), i});
		}
	}

	co_return 42;
}

TEST_CASE("capy - cancellation")
{
	capy::thread_pool thd_pool(8);
	capy::Executor auto ex = thd_pool.get_executor();
	
	auto on_complete = [](std::expected<int, std::tuple<std::error_code, int>> result) {
		if (result)
		{
			std::cout << "Result: " << *result << "\n";
		}
		else
		{
			auto [errc, index] = result.error();
			std::cout << "Task was cancelled: (" << errc.message() << ", " << index << ")\n";
		}
	};

	std::stop_source ssource;

	capy::run_async(ex, ssource.get_token(), on_complete)(long_running_task());
	
	std::this_thread::sleep_for(100ms);

	ssource.request_stop();

	thd_pool.join();
}


//////////////////////////////////////////
// when_all

capy::task<capy::results<int>> task1()
{
	co_return {std::error_code{}, 42};
}

capy::task<capy::results<int>> task2()
{
	co_return {std::error_code{}, 665};
}

capy::task<int> task3()
{
	auto [ec, r1, r2] = co_await capy::when_all(task1(), task2());

	if (ec)
	{
		throw std::system_error(ec);
	}

	co_return r1 + r2;
}

TEST_CASE("capy - when_all")
{
	capy::thread_pool thd_pool;
    capy::Executor auto ex = thd_pool.get_executor();

	auto on_complete = [](int result) {
        std::cout << "Result: " << result << "\n";
    };

	capy::run_async(ex, on_complete)(task3());

    thd_pool.join(); // Wait for all tasks in the thread pool to complete.
}

capy::task<capy::results<int>> find_next_prime(int id, int start)
{
    synced_cout() << "Task " << id << " started searching for prime from " << start
                  << " on thread#" << std::this_thread::get_id() << "\n";

    std::stop_token stoken = co_await capy::this_coro::stop_token; // get the stop token from environment

    int candidate = start;
    while (true)
    {
        if (stoken.stop_requested())
        {
            synced_cout() << "Task " << id << " was requested to stop\n";
            co_return capy::results<int>{std::make_error_code(std::errc::operation_canceled), 0};
        }

        bool is_prime = true;
        for (int i = 2; i * i <= candidate; ++i)
        {
            if (candidate % i == 0)
            {
                is_prime = false;
                break;
            }
        }

        if (is_prime)
        {
            synced_cout() << "Task " << id << " found prime: " << candidate << "\n";
            co_return capy::results<int>{std::error_code{}, candidate};
        }

        ++candidate;
    }
}

TEST_CASE("primes with when_all")
{
	
}
