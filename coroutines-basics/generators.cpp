#include <catch2/catch_test_macros.hpp>
#include <coroutine>
#include <iostream>
#include <list>
#include <generator>

template <typename T>
class Generator
{
public:
    struct promise_type
    {
        T value_;
        std::exception_ptr exception_;

        Generator get_return_object()
        {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value)
        {
            value_ = std::move(value);
            return {};
        }

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        void return_void() { }
    };

    Generator(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    { }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    ~Generator()
    {
        if (handle_)
            handle_.destroy();
    }

    struct iterator
    {
        using value_type = T;
        using reference = T&;

        reference operator*() const
        {
            return handle_.promise().value_;
        }

        iterator& operator++() // ++it
        {
            handle_.resume();

            if (handle_.done())
            {
                auto& promise = handle_.promise();

                if (promise.exception_)
                    std::rethrow_exception(promise.exception_);

                handle_ = nullptr;
            }

            return *this;
        }

        iterator operator++(int) // it++
        {
            iterator temp = *this;
            ++(*this);
            return temp;
        }

        bool operator==(const iterator& other) const
        {
            return handle_ == other.handle_;
        }

        iterator()
            : handle_{nullptr}
        { }

        explicit iterator(std::coroutine_handle<promise_type> handle)
            : handle_{handle}
        { }

        std::coroutine_handle<promise_type> handle_;
    };

    iterator begin()
    {
        handle_.resume();

        if (handle_.done())
        {
            auto& promise = handle_.promise();

            if (promise.exception_)
                std::rethrow_exception(promise.exception_);

            handle_ = nullptr;
            return iterator{};
        }

        return iterator{handle_};
    }

    iterator end()
    {
        return iterator{};
    }

private:
    std::coroutine_handle<promise_type> handle_;
};

Generator<int> sequence(int start, int end, int step = 1)
{
    for (int i = start; i < end; i += step)
    {
        co_yield i;
    }
}

TEST_CASE("Generator")
{
    SECTION("for with iterators")
    {
        Generator<int> gen = sequence(1, 10);

        std::vector<int> vec;
        for (auto it = gen.begin(); it != gen.end(); ++it)
        {
            vec.push_back(*it);
        }

        REQUIRE(vec == std::vector{1, 2, 3, 4, 5, 6, 7, 8, 9});
    }

    SECTION("range-based for")
    {
        for (int item : sequence(1, 10))
        {
            std::cout << item << "\n";
        }
    }
}

template <typename T1, typename T2>
auto zip(const T1& rng_1, const T2& rng_2) 
-> std::generator<std::tuple<std::ranges::range_value_t<T1>, std::ranges::range_value_t<T2>>>
{
    auto it1 = rng_1.begin();
    auto it2 = rng_2.begin();

    for (; it1 != rng_1.end() && it2 != rng_2.end(); ++it1, ++it2)
    {
        co_yield std::tuple{*it1, *it2};
    }
}

TEST_CASE("zip")
{
    std::vector<std::string> words = {"one", "two", "three"};
    std::vector numbers = {1, 2, 3};

    for (auto [v1, v2] : zip(words, numbers))
    {
        std::cout << "v1: " << v1 << " v2: " << v2 << "\n";
    }

    std::vector<int> indexes = {1, 2, 3};
    std::list<float> values = {3.14, 2.71, 6.28};

    for (auto [i, v] : zip(indexes, values))
    {
        std::cout << "{" << i << ", " << v << "}\n";
    }
}

template<typename T>
struct Tree
{
    T value;
    Tree *left{}, *right{};

    std::generator<const T&> traverse_inorder() const
    {
        if (left)
            co_yield std::ranges::elements_of(left->traverse_inorder());

        co_yield value;

        if (right)
            co_yield std::ranges::elements_of(right->traverse_inorder());
    }
};

TEST_CASE("Tree traversing with genrator")
{
    Tree<char> tree[]
    {
                                    {'D', tree + 1, tree + 2},
        //                            │
        //            ┌───────────────┴────────────────┐
        //            │                                │
                    {'B', tree + 3, tree + 4},       {'F', tree + 5, tree + 6},
        //            │                                │
        //  ┌─────────┴─────────────┐      ┌───────────┴─────────────┐
        //  │                       │      │                         │
        {'A'},                  {'C'}, {'E'},                    {'G'}
    };

    for (char x : tree->traverse_inorder())
        std::cout << x << ' ';
    std::cout << '\n';
}