#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "io_context.hpp"

using namespace std::literals;

namespace
{
    Detached sleeper(IoContext& ctx, std::string name, std::chrono::milliseconds d, std::vector<std::string>& log)
    {
        std::cout << name << " started\n";
        co_await delay(ctx, d);
        std::cout << name << " woke up after " << d.count() << "ms\n";
        log.push_back(name);
    }

    Detached sequence(IoContext& ctx, int& steps)
    {
        for (int i = 0; i < 3; ++i)
        {
            co_await delay(ctx, 10ms);
            ++steps;
        }
    }
} // namespace

TEST_CASE("delay suspends for at least the given period")
{
    IoContext ctx;
    std::vector<std::string> log;

    auto start = std::chrono::steady_clock::now();
    sleeper(ctx, "A", 50ms, log);
    ctx.run();

    CHECK(std::chrono::steady_clock::now() - start >= 50ms);
    CHECK(log == std::vector<std::string>{"A"});
}

TEST_CASE("concurrent delays complete in order of expiration")
{
    IoContext ctx;
    std::vector<std::string> log;

    sleeper(ctx, "slow", 80ms, log);
    sleeper(ctx, "fast", 10ms, log);
    sleeper(ctx, "medium", 40ms, log);
    CHECK(log.empty());

    ctx.run();

    CHECK(log == std::vector<std::string>{"fast", "medium", "slow"});
}

TEST_CASE("sequential delays in one coroutine")
{
    IoContext ctx;
    int steps = 0;

    sequence(ctx, steps);
    ctx.run();

    CHECK(steps == 3);
}
