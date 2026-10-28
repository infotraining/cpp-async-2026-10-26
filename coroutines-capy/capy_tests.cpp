#include <algorithm>
#include <boost/capy.hpp>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <syncstream>
#include <thread>
#include <vector>
#include <boost/capy/test/stream.hpp>

std::osyncstream synced_cout()
{
    return std::osyncstream(std::cout);
}

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
        catch (const std::runtime_error& e)
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

    for (int i = 0; i < 100; ++i)
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

capy::task<capy::results<int>> task1()
{
    co_return {std::error_code{}, 42};
}

capy::task<capy::results<int>> task2()
{
    co_return {std::error_code{}, 665};
}

capy::task<int> task3()
{
    auto [ec, r1, r2] = co_await capy::when_all(task1(), task2());

    if (ec)
    {
        throw std::system_error(ec);
    }

    co_return r1 + r2;
}

TEST_CASE("capy - when_all")
{
    capy::thread_pool thd_pool;
    capy::Executor auto ex = thd_pool.get_executor();

    auto on_complete = [](int result) {
        std::cout << "Result: " << result << "\n";
    };

    capy::run_async(ex, on_complete)(task3());

    thd_pool.join(); // Wait for all tasks in the thread pool to complete.
}

capy::task<capy::results<int>> find_next_prime(int id, int start)
{
    synced_cout() << "Task " << id << " started searching for prime from " << start
                  << " on thread#" << std::this_thread::get_id() << "\n";

    std::stop_token stoken = co_await capy::this_coro::stop_token; // get the stop token from environment

    int candidate = start;
    while (true)
    {
        if (stoken.stop_requested())
        {
            synced_cout() << "Task " << id << " was requested to stop\n";
            co_return capy::results<int>{std::make_error_code(std::errc::operation_canceled), 0};
        }

        bool is_prime = true;
        for (int i = 2; i * i <= candidate; ++i)
        {
            if (candidate % i == 0)
            {
                is_prime = false;
                break;
            }
        }

        if (is_prime)
        {
            synced_cout() << "Task " << id << " found prime: " << candidate << "\n";
            co_return capy::results<int>{std::error_code{}, candidate};
        }

        ++candidate;
    }
}

capy::task<void> alamakota()
{
    auto [ec, r1, r2, r3] = co_await capy::when_all(find_next_prime(0, 10000), find_next_prime(1, 20000), find_next_prime(2, 30000));

    if (ec)
    {
        throw std::system_error(ec);
    }

    synced_cout() << "R1: " << r1 << ", R2: " << r2 << ", R3: " << r3 << "\r";
}

TEST_CASE("primes with when_all")
{
    capy::thread_pool thd_pool;
    capy::Executor auto ex = thd_pool.get_executor();

    capy::run_async(ex)(alamakota());

    thd_pool.join();
}

//////////////////////////////////////
// Monte-Carlo Pi

capy::task<capy::results<uintmax_t>> calc_hits(const uintmax_t count)
{
    const auto seed = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::mt19937_64 rnd_gen(seed);
    std::uniform_real_distribution<double> rnd_distr(0.0, 1.0);

    uintmax_t hits = 0;
    for (long n = 0; n < count; ++n) // hot-loop
    {
        double x = rnd_distr(rnd_gen);
        double y = rnd_distr(rnd_gen);
        if (x * x + y * y < 1)
            ++hits;
    }

    co_return {std::error_code{}, hits};
}

capy::task<double> monte_carlo_pi(uintmax_t n)
{
    const size_t no_of_cores = std::thread::hardware_concurrency();
    const uintmax_t n_per_core = n / no_of_cores;

    std::vector<capy::task<capy::results<uintmax_t>>> tasks;

    for (size_t i = 0; i < no_of_cores; ++i)
        tasks.push_back(calc_hits(n_per_core));

    auto [ec, results] = co_await capy::when_all(std::move(tasks));

    if (ec)
        throw std::system_error(ec);

    uintmax_t hits{};

    for (auto r : results)
    {
        hits += r;
    }

    const double pi = static_cast<double>(hits) / n * 4;

    co_return pi;
}

TEST_CASE("Monte-Carlo Pi")
{
    constexpr uintmax_t N = 10'000'000;

    capy::thread_pool thd_pool;
    capy::Executor auto ex = thd_pool.get_executor();

    auto on_complete = [](double pi) {
        std::cout << "Monte-Carlo Pi: " << pi << "\n";
    };

    capy::run_async(ex, on_complete)(monte_carlo_pi(N));

    thd_pool.join();
}

template <typename... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts>
struct inspect_result
{
    std::variant<Ts...>& v;

    explicit inspect_result(std::variant<Ts...>& var)
        : v(var)
    { }

    template <typename... Us>
    auto with(Us&&... us)
    {
        return std::visit(overload{std::forward<Us>(us)...}, v);
    }
};

capy::task<int> find_prime()
{
    auto vr = co_await capy::when_any(
        find_next_prime(1, 100'000),
        find_next_prime(2, 200'000),
        find_next_prime(3, 400'000));
		
	std::cout << "Task#" << vr.index() << " is winner!\n";
	
	int prime_result{};

	inspect_result(vr).with(
		[](std::error_code ec) {
			std::cout << "Completed with error: " << ec.message() << "\n";
			throw std::system_error(ec);
		},
		[&prime_result](int prime) {
			std::cout << "Found prime: " << prime << "\n";
			prime_result = prime;
		}
	);

	co_return prime_result;
}

TEST_CASE("capy - when_any")
{
    capy::thread_pool thd_pool;
    capy::Executor auto ex = thd_pool.get_executor();

    auto on_complete = [](int result) {
        std::cout << "Found prime: " << result << "\n";
    };

    auto on_error = [](std::exception_ptr eptr) {
        try
        {
            if (eptr)
                std::rethrow_exception(eptr);
        }
        catch (const std::exception& e)
        {
            std::cout << "Caught exception: " << e.what() << "\n";
        }
    };

    capy::run_async(ex, on_complete, on_error)(find_prime());

    thd_pool.join(); // Wait for all tasks in the thread pool to complete.
}

struct PrintVisitor
{
	void operator()(int n) 
	{
		std::cout << "int: " << n << "\n";
	}

	void operator()(double d)
	{
		std::cout << "double: " << d << "\n";
	}

	void operator()(const std::string& s)
	{
		std::cout << "string: " << s << "\n";
	}
};

TEST_CASE("std::variant")
{
	std::variant<int, double, std::string> vr = 42;

	REQUIRE(std::holds_alternative<int>(vr) == true);
	int counter = std::get<int>(vr);
	REQUIRE(counter == 42);
	
	vr = std::string("text");

	std::string text = std::get<std::string>(vr);
	REQUIRE(text == "text");
	
	REQUIRE_THROWS_AS(std::get<double>(vr), std::bad_variant_access);

	REQUIRE(vr.index() == 2);

	PrintVisitor prn_visitor;
	std::visit(prn_visitor, vr);

	vr = 3.14;

	int result = std::visit(
		overload {	
			[](int n) -> int { return n; },
			[](double d) -> int { return d; },
			[](const std::string& s) -> int { return s.length(); }
		},
		vr);

	std::cout << "result:" << result << "\n";
}

capy::task<> echo_session(capy::any_stream& stream)
{
    char buffer[1024];

    for (;;)
    {
        // ec: std::error_code, n: std::size_t
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buffer));

        // wec: std::error_code, wn: std::size_t
        auto [wec, wn] = co_await capy::write(stream, capy::const_buffer(buffer, n));

        if (ec)
            co_return;

        if (wec)
            co_return;
    }
}

TEST_CASE("capy - echo session")
{
    auto [a, b] = capy::test::make_stream_pair();
    b.provide("Hello, ");
    b.provide("World!\n");
    b.close();

    // Using pointer construction (&a) for reference semantics - the
    // wrapper does not take ownership, so a must outlive stream.
    capy::any_stream stream{&a}; // any_stream
    capy::test::run_blocking()(echo_session(stream));

    std::cout << "Echo output: " << b.data() << "\n";
}