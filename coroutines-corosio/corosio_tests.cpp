#include <algorithm>
#include <boost/capy.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/corosio.hpp>
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

std::osyncstream synced_cout()
{
    return std::osyncstream(std::cout);
}

namespace capy = boost::capy;
namespace corosio = boost::corosio;

using namespace std::literals;

capy::task<std::uint64_t>
compute_fnv1a(char const* data, std::size_t len)
{
    std::cout << "Computing FNV-1a hash for " << std::string_view(data, len)
              << " in thread#" << std::this_thread::get_id() << "\n";

    constexpr std::uint64_t basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;

    std::uint64_t h = basis;
    for (std::size_t i = 0; i < len; ++i)
    {
        h ^= static_cast<unsigned char>(data[i]);
        h *= prime;
    }
    co_return h;
}

capy::task<std::string> to_hex(std::uint64_t value)
{
    constexpr char hex_chars[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        result[i] = hex_chars[value & 0xf];
        value >>= 4;
    }
    co_return result;
}

capy::task<>
do_session(corosio::tcp_socket sock, capy::thread_pool& pool)
{
    std::cout << "Starting new session\n";
    std::cout << "Waiting for data from client...\n";

    char buf[4096];

    auto [ec, n] = co_await sock.read_some(capy::mutable_buffer(buf, sizeof(buf)));

    if (ec)
    {
        sock.close();
        co_return;
    }

    std::cout << "Data received: " << std::string_view(buf, n) << "\n";

    auto hash = co_await capy::run(pool.get_executor())(compute_fnv1a(buf, n));

    auto hash_hex = co_await capy::run(pool.get_executor())(to_hex(hash));

    std::cout << "Computed hash: " << hash_hex << std::endl;
    std::cout << "Sending result to client...\n";

    auto [wec, wn] = co_await capy::write(sock, capy::const_buffer(hash_hex.data(), hash_hex.size()));

    std::cout << "Result sent to client\n";
    sock.close();
}

capy::task<> do_accept(corosio::io_context& ioc,
    corosio::tcp_acceptor& acc,
    capy::thread_pool& pool)
{
    int session_count{};

    for (;;)
    {
        corosio::tcp_socket peer(ioc);

        auto [ec] = co_await acc.accept(peer);

        ++session_count;

        if (ec)
            break;

        std::cout << "\n---------------------------------------------\n";
        std::cout << "Accepted new connection - session count: " << session_count << "\n";

        capy::run_async(ioc.get_executor())(do_session(std::move(peer), pool));
    }
}

TEST_CASE("Hash.Server")
{
    corosio::io_context ioc;
    capy::thread_pool pool;

    int port = 12345;

    corosio::tcp_acceptor acc(ioc, corosio::endpoint(port));

    std::cout << "Hash server is listening on port " << port << "\n";

    capy::run_async(ioc.get_executor())(do_accept(ioc, acc, pool));

    ioc.run();
    pool.join();
}

capy::task<int> do_client(corosio::io_context& ioc, std::string message)
{
    corosio::tcp_socket sock(ioc);
    corosio::endpoint ep(corosio::ipv4_address::loopback(), 12345);

    auto [ec] = co_await sock.connect(ep);

    if (ec)
    {
        std::cout << "Failed to connect to server: " << ec.message() << "\n";
        co_return -1;
    }

    std::cout << "Connected to server\n";

    std::cout << "Sending message to server: " << message;

    auto [wec, wn] = co_await capy::write(sock, capy::const_buffer(message.data(), message.size()));

    if (wec)
    {
        std::cerr << "Failed to send message to server: " << wec.message() << "\n";
        co_return -1;
    }
    else
    {
        std::cout << "Message sent successfully (" << wn << " bytes)\n";
    }

    char buf[4096];

    auto [rec_ec, rec_n] = co_await sock.read_some(capy::mutable_buffer(buf, sizeof(buf)));

    if (!rec_ec)
    {
        std::string_view response(buf, rec_n);
        std::cout << "Server response: " << response << "\n";
    }
    else
    {
        std::cerr << "Failed to read server response: " << rec_ec.message() << "\n";
    }

    sock.close();

    co_return 0;
}

TEST_CASE("Hash.Client")
{
    corosio::io_context ioc;

    capy::run_async(ioc.get_executor())(do_client(ioc, "Hello, server!\n"));

    ioc.run();
}

auto wrap(auto inner) -> capy::task<capy::results<decltype(inner.await_resume())>>
{
    co_return capy::results<decltype(inner.await_resume())>{
        std::error_code(), co_await std::move(inner)};
};

capy::task<void> batch(corosio::io_context& ioc, std::vector<std::string> messages)
{
    std::vector<capy::task<capy::results<int>>> tasks;

    for(const auto& message : messages)
    {
        tasks.push_back(wrap(do_client(ioc, message)));
    }

    auto [ec, results] = co_await capy::when_all(std::move(tasks));
}

TEST_CASE("Hash.Client.Batch")
{
    corosio::io_context ioc;

    std::vector<std::string> messages = {
        "Hello, server!\n",
        "Another message to server\n",
        "Yet another message to server\n",
        "Final message to server\n"};

    capy::run_async(ioc.get_executor())(batch(ioc, std::move(messages)));

    ioc.run();
}

// struct port
// {
//     std::vector<std::coroutine_handle<>> pending_ops_;

//     auto write()
//     {
//         struct Awaiter
//         {
//             port& p;

//             bool await_ready() { return false; }

//             void await_suspend(std::coroutine_handle<> awaiting_coro)
//             {
//                 p.pending_ops_.push_back(awaiting_coro);
//             }
//         };

//         return Awaiter{*this};
//     }

//     void run()
//     {
//         // waiting for event 
//         // find corohandle in pending_op_;
//         pending_ops_[2].resume();

//     }
// };

// capy::task<> do_session_with_serial_port(port& p)
// {
//     co_await p.write("Msg#1");


//     co_await p.read()
// }