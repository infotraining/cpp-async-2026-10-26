#include <boost/capy.hpp>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <latch>

#define LOG_ENABLED
#include "task_logger.hpp"

namespace capy = boost::capy;

TEST_CASE("Producer-Consumer", "[capy][producer-consumer]")
{
    capy::thread_pool pool;
    capy::strand strand(pool.get_executor()); // strand - serializes execution of tasks posted to it, ensuring that they do not run concurrently

    std::latch done{1};

    capy::async_event data_ready; // async_event - allows coroutines to wait for an event to be set, and to be notified when it is set
    int shared_value = 0;

    auto on_complete = [&done](auto&&...) {
        done.count_down();
    };

    auto on_error = [&done](std::exception_ptr eptr) {
        done.count_down();
    };

    auto producer = [&]() -> capy::task<capy::results<>> {
        LOGGER << "Producer: preparing data..." << std::endl;

        shared_value = 42;

        LOGGER << "Producer: data ready, notifying consumer..." << std::endl;

        data_ready.set(); // Notify the consumer that data is ready

        co_return capy::results<>{};
    };

    auto consumer = [&]() -> capy::task<capy::results<>> {
        LOGGER << "Consumer: waiting for data..." << std::endl;

        co_await data_ready.wait(); // Wait for the producer to signal that data is ready

        LOGGER << "Consumer: received data: " << shared_value << std::endl;

        co_return capy::results<>{};
    };

    auto run_both = [&]() -> capy::task<> {
        (void)co_await capy::when_all(producer(), consumer());
    };

    capy::run_async(strand, on_complete, on_error)(run_both());

    done.wait();

    SUCCEED();
}