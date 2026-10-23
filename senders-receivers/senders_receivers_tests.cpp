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
}
