#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <exec/single_thread_context.hpp>
#include <exec/static_thread_pool.hpp>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <stdexec/execution.hpp>
#include <string>
#include <vector>
#include <syncstream>
#include <exec/task.hpp>

using namespace std::literals;

TEST_CASE("Senders/Receivers")
{
    namespace execution = stdexec;
    using namespace exec;

    SECTION("basics")
    {
        static_thread_pool thread_pool{8};

        execution::scheduler auto thd_pool = thread_pool.get_scheduler();

        execution::sender auto start = execution::schedule(thd_pool);

        execution::sender auto hi = execution::then(start, [] {
            std::cout << "Hello, World! Have an int." << std::endl;
            return 13;
        });

        execution::sender auto add_42 = execution::then(hi, [](int arg) {
            std::cout << "Adding 42 to " << arg << std::endl;
            return arg + 42;
        });

        auto [result] = execution::sync_wait(add_42).value();

        CHECK(result == 55);
    }

    SECTION("with pipes")
    {
        static_thread_pool thread_pool{8};

        execution::scheduler auto thd_pool = thread_pool.get_scheduler();

        execution::sender auto work = execution::schedule(thd_pool)
            | execution::then([] {
                  std::cout << "Hello, World! Have an int." << std::endl;
                  return 13;
              })
            | execution::then([](int value) {
                  std::cout << "Received: " << value << std::endl;
                  return value + 42;
              });

        auto [result] = execution::sync_wait(work).value();

        CHECK(result == 55);
    }
}

TEST_CASE("when_all", "[stdexec]")
{
    using namespace stdexec;
    using namespace exec;

    static_thread_pool thread_pool{3};

    auto scheduler = thread_pool.get_scheduler();

    auto func = [](int i) -> int {
        return i * i;
    };

    SECTION("gathers results from senders")
    {
        auto work = when_all(
            starts_on(scheduler, just(1) | then(func)),
            starts_on(scheduler, just(2) | then(func)),
            starts_on(scheduler, just(3) | then(func)));

        auto [i, j, k] = sync_wait(std::move(work)).value();

        REQUIRE(i == 1);
        REQUIRE(j == 4);
        REQUIRE(k == 9);
    }

    SECTION("throws an error if any sender sends an error")
    {
        auto work = when_all(
            starts_on(scheduler, just(1) | then(func)),
            starts_on(scheduler, just(2) | then(func)),
            starts_on(scheduler, just() | then([] { throw std::runtime_error("Error from sender!"); return 0; })));

        CHECK_THROWS_AS(sync_wait(std::move(work)), std::runtime_error);
    }

    SECTION("if any sender is stopped you must recover with let_stopped")
    {
        sender auto s1 = just(42);
        sender auto s2 = just_stopped() | let_stopped([] { return just(665); });

        auto work = when_all(s1, s2);

        REQUIRE(sync_wait(std::move(work)).value() == std::tuple{42, 665});
    }
}

TEST_CASE("switching execution contexts - threads", "[stdexec]")
{
    namespace execution = stdexec;
    using namespace exec;

    exec::single_thread_context thread_context_1;
    exec::single_thread_context thread_context_2;

    execution::scheduler auto scheduler_1 = thread_context_1.get_scheduler();
    execution::scheduler auto scheduler_2 = thread_context_2.get_scheduler();

    auto step_1 = [](int i) {
        std::cout << "Work#1 on thread " << std::this_thread::get_id() << std::endl;
        return i * 2;
    };

    auto step_2 = [](int i) {
        std::cout << "Work#2 on thread " << std::this_thread::get_id() << std::endl;
        return i + 1;
    };

    execution::sender auto work = execution::starts_on(scheduler_1, execution::just(42))
        | execution::then(step_1)
        | execution::continues_on(scheduler_2)
        | execution::then(step_2)
        | execution::continues_on(scheduler_1)
        | execution::then(step_1);

    auto [result] = execution::sync_wait(std::move(work)).value();

    CHECK(result == 170);
}

exec::task<int> coro_1()
{
    int result = co_await stdexec::just(2);

    co_return 42 * result;
}

TEST_CASE("coroutine as sender")
{
    auto [result] = stdexec::sync_wait(coro_1()).value();
    
    REQUIRE(result == 84);
}

TEST_CASE("coroutines", "[stdexec][coroutines]")
{
    SECTION("using Sender as awaitable")
    {
        exec::static_thread_pool thread_pool{3};

        auto scheduler = thread_pool.get_scheduler();       

        stdexec::sender auto snd = stdexec::on(scheduler, 
            stdexec::just(8) | stdexec::then([](int i) { return i * i; }));

        auto coro_task = [snd = std::move(snd)]() -> exec::task<std::string> {
            int result = co_await snd;
            co_return "Value: "s + std::to_string(result);
        };

        auto [result] = stdexec::sync_wait(coro_task()).value();
        CHECK(result == "Value: 64");
    }
}