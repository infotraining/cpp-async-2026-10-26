#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <iostream>
#include <algorithm>
#include <numeric>

TEST_CASE("sort")
{
	std::vector vec = {1, 2, 3, 4};
	std::sort(vec.begin(), vec.end(), std::greater{});

	REQUIRE(std::is_sorted(vec.rbegin(), vec.rend()));
}

