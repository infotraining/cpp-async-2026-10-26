#include "concurrent_queue.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <expected>
#include <iostream>
#include <latch>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>
#include <syncstream>
#include <random>
#include <future>

using namespace AsyncLab;
using namespace std::literals;

std::optional<int> find(const std::vector<int>& vec, int value)
{
    if (vec.empty())
        return std::nullopt;

    for (int item : vec)
    {
        if (item == value)
            return item;
    }

    return std::nullopt;
}

std::osyncstream synced_cout()
{
    return std::osyncstream(std::cout);
}

TEST_CASE("std::optional")
{
    std::optional<int> opt_int;

    REQUIRE(opt_int.has_value() == false);
    REQUIRE(opt_int == std::nullopt);

    opt_int = 42;
    REQUIRE(opt_int.has_value() == true);

    if (opt_int)
        REQUIRE(*opt_int == 42);

    std::vector<int> vec = {1, 2, 3, 665};

    std::optional<int> find_result = find(vec, 42);

    if (find_result)
        std::cout << "Found: " << *find_result << "\n";

    REQUIRE_THROWS_AS(find_result.value(), std::bad_optional_access);
}

TEST_CASE("ConcurrentQueue")
{
    ConcurrentQueue<int> queue;

    SECTION("pop() - basic operations")
    {
        queue.push(1);
        queue.push(2);
        queue.push(3);

        std::optional<int> value;

        value = queue.pop();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 1);

        value = queue.pop();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 2);

        value = queue.pop();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 3);
    }

    SECTION("try_pop")
    {
        std::expected<int, ConqueueStatus> result = queue.try_pop();

        REQUIRE(result.has_value() == false);
        REQUIRE(result.error() == ConqueueStatus::Empty);
    }

    SECTION("client waits when poping from empty")
    {
        std::optional<int> item;

        std::chrono::high_resolution_clock::time_point t1;

        std::thread thd{[&queue, &item, &t1] {
            item = queue.pop();
            t1 = std::chrono::high_resolution_clock::now();
        }};

        std::this_thread::sleep_for(100ms);
        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        queue.push(1);
        thd.join();
        REQUIRE(t1 >= t2);
        REQUIRE(item.value() == 1);
    }
}

TEST_CASE("ConcurrentQueue - close")
{
    ConcurrentQueue<int> queue;

    SECTION("after close push returns false")
    {
        queue.close();

        REQUIRE(queue.is_closed() == true);

        bool result = queue.push(42);
        REQUIRE(result == false);
    }

    SECTION("after close try_pop returns Closed")
    {
        queue.close();
        std::expected<int, ConqueueStatus> result = queue.try_pop();
        REQUIRE(result.has_value() == false);
        REQUIRE(result.error() == ConqueueStatus::Closed);
    }

    SECTION("threads that waits on empty queue are unblocked when queue is closed")
    {
        std::latch ready_for_pop{1};
        std::atomic<bool> was_unblocked{false};
        std::optional<int> item;

        auto pop_worker = std::thread([&] {
            ready_for_pop.count_down();
            item = queue.pop();
            was_unblocked = true;
        });

        ready_for_pop.wait();
        std::this_thread::sleep_for(100ms);
        queue.close();

        pop_worker.join();
        REQUIRE(was_unblocked == true);
        REQUIRE(item == std::nullopt);
    }
}

void background_work(std::atomic<bool>& done)
{
    std::this_thread::sleep_for(100ms);
    done = true;
}

void save_to_file(const std::string& filename)
{
    synced_cout() << "Saving to file: " << filename
              << " in thread#" << std::this_thread::get_id() << std::endl;

    std::this_thread::sleep_for(250ms);

    synced_cout() << "File saved: " << filename << std::endl;
}

TEST_CASE("ThreadPool")
{
    SECTION("submit - work is executed in worker thread")
    {
        std::atomic<bool> is_done = false;

        {
            ThreadPool thd_pool(8);

            thd_pool.submit([&]() { background_work(is_done); });
        }

        REQUIRE(is_done == true);
    }
}

TEST_CASE("ThreadPool - many tasks")
{
    ThreadPool thd_pool(8);

    for (size_t i = 1; i <= 20; ++i)
    {
        thd_pool.submit([i] { save_to_file(std::string("file#") + std::to_string(i)); });
    }
}

int calculate_square(int x)
{
    synced_cout() << "Starting calculation for " << x << " in " << std::this_thread::get_id() << std::endl;

    std::random_device rd;
    std::uniform_int_distribution<> distr(100, 500);

    std::this_thread::sleep_for(std::chrono::milliseconds(distr(rd)));

    if (x % 3 == 0)
        throw std::runtime_error("Error#3");

    return x * x;
}

TEST_CASE("std::packaged_task + std::future")
{
    std::packaged_task<int(int)> pt(&calculate_square);
    std::future<int> f_square = pt.get_future();

    //pt(13); // synchronous call

    std::jthread thd{std::move(pt), 13}; // // asynchronous call

    int result = f_square.get(); // blocking call

    REQUIRE(result == 13 * 13);
}

TEST_CASE("std::async")
{
    std::future<int> f_square = std::async(std::launch::async, &calculate_square, 13);

    int result = f_square.get(); // blocking call

    REQUIRE(result == 13 * 13);
}

TEST_CASE("ThreadPool + std::future")
{
    std::vector<std::future<int>> f_results;

    ThreadPool thd_pool(8);

    for(size_t i = 1; i <= 20; ++i)
        f_results.push_back(thd_pool.submit([i] { return calculate_square(i); }));

    for(auto& f_result : f_results)
    {
        try
        {
            int res = f_result.get();
            synced_cout() << "Result: " << res << "\n";
        }
        catch(const std::exception& e)
        {
            synced_cout() << e.what() << '\n';
        }
    }
}