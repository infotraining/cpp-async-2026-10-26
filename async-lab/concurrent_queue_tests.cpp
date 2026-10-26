#include "concurrent_queue.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>
#include <expected>
#include <chrono>
#include <latch>

using namespace AsyncLab;
using namespace std::literals;

std::optional<int> find(const std::vector<int>& vec, int value)
{
    if (vec.empty())
        return std::nullopt;

    for(int item : vec)
    {
        if (item == value)
            return item;
    }
    
    return std::nullopt;
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