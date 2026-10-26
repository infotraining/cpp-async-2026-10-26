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

using namespace AsyncLab;
using namespace std::literals;

TEST_CASE("ConcurrentQueue - basic operations")
{
    ConcurrentQueue<int> queue;

    REQUIRE(true);
}