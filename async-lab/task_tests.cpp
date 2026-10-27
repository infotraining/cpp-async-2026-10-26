#include "task.hpp"

#include <stdexec/execution.hpp>
#include <catch2/catch_test_macros.hpp>

using AsyncLab::Task;
using stdexec::sync_wait;

Task<int> simple_task()
{
    co_return 42;
}

Task<std::string> nested_task(int value)
{
    int result = co_await simple_task();

    co_return "#" + std::to_string(result);
}

TEST_CASE("Task")
{
    SECTION("returns value by co_return")
    {
        Task<int> tsk = simple_task();

        auto [value] = sync_wait(std::move(tsk)).value();

        REQUIRE(value == 42);
    }

    SECTION("tasks can be nested")
    {
        Task<std::string> tsk = nested_task(42);

        auto [value] = sync_wait(std::move(tsk)).value();

        REQUIRE(value == "#42");
    }

    SECTION("rethrows exceptions")
    {
        Task<int> tsk = []() -> Task<int> {
            throw std::runtime_error("error");
            co_return 0;
        }();

        REQUIRE_THROWS_AS(sync_wait(std::move(tsk)), std::runtime_error);
    }
}