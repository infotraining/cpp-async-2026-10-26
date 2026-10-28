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

#include <boost/capy.hpp>

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

