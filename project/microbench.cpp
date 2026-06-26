// microbench: simple two-node unidirectional ping-pong benchmark using standalone asio.
// Usage:
//   Server:  ./microbench -s <ip> <port> <block_size>
//   Client:  ./microbench -c <ip> <port> <block_size> <iterations>
//
// Example:
//   # On node A:
//   ./microbench -s 0.0.0.0 8888 1048576
//   # On node B:
//   ./microbench -c 10.10.1.2 8888 1048576 10000

#include <asio.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void print_usage(const char *prog)
{
    std::cerr << "Usage:\n"
              << "  Server: " << prog << " -s <bind_ip> <port> <block_size>\n"
              << "  Client: " << prog << " -c <server_ip> <port> <block_size> <iterations>\n"
              << "\nExample:\n"
              << "  " << prog << " -s 0.0.0.0 8888 1048576\n"
              << "  " << prog << " -c 10.10.1.2 8888 1048576 10000\n";
    std::exit(1);
}

static void run_server(const std::string &bind_ip, int port, size_t block_size)
{
    asio::io_context io;
    asio::ip::tcp::acceptor acceptor(io,
        asio::ip::tcp::endpoint(asio::ip::address::from_string(bind_ip), static_cast<unsigned short>(port)));

    std::cout << "[server] listening on " << bind_ip << ":" << port
              << ", block_size=" << block_size << " bytes (" << static_cast<double>(block_size) / 1024.0 / 1024.0 << " MB)"
              << std::endl;

    std::vector<char> buf(block_size);

    // accept one connection, then loop receiving until peer closes
    asio::ip::tcp::socket sock(io);
    acceptor.accept(sock);
    std::cout << "[server] client connected, receiving..." << std::endl;

    size_t total_bytes = 0;
    size_t iterations = 0;

    auto t_start = std::chrono::steady_clock::now();

    while (true)
    {
        asio::error_code ec;
        size_t n = asio::read(sock, asio::buffer(buf.data(), block_size), ec);
        if (ec)
        {
            if (ec == asio::error::eof)
                std::cout << "[server] peer closed connection." << std::endl;
            else
                std::cerr << "[server] read error: " << ec.message() << std::endl;
            break;
        }
        total_bytes += n;
        ++iterations;

        if (iterations % 1000 == 0)
        {
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t_start).count();
            double throughput_mbps = (static_cast<double>(total_bytes) / 1024.0 / 1024.0) / elapsed;
            std::cout << "[server] recv " << iterations << " blocks, "
                      << total_bytes / 1024.0 / 1024.0 << " MB, "
                      << "throughput: " << throughput_mbps << " MB/s" << std::endl;
        }
    }

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t_start).count();
    double throughput_mbps = (static_cast<double>(total_bytes) / 1024.0 / 1024.0) / elapsed;

    std::cout << "[server] done. total " << iterations << " blocks, "
              << total_bytes / 1024.0 / 1024.0 << " MB in " << elapsed << " s, "
              << "throughput: " << throughput_mbps << " MB/s" << std::endl;
}

static void run_client(const std::string &server_ip, int port, size_t block_size, size_t iterations)
{
    asio::io_context io;
    asio::ip::tcp::resolver resolver(io);

    std::cout << "[client] connecting to " << server_ip << ":" << port << " ..." << std::endl;
    asio::ip::tcp::socket sock(io);
    asio::connect(sock, resolver.resolve({server_ip, std::to_string(port)}));
    std::cout << "[client] connected. sending " << iterations << " blocks of "
              << block_size << " bytes (" << static_cast<double>(block_size) / 1024.0 / 1024.0 << " MB)"
              << std::endl;

    std::vector<char> buf(block_size, 'A');  // fill with dummy data

    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < iterations; ++i)
    {
        asio::error_code ec;
        asio::write(sock, asio::buffer(buf.data(), block_size), ec);
        if (ec)
        {
            std::cerr << "[client] write error at iteration " << i << ": " << ec.message() << std::endl;
            break;
        }

        if ((i + 1) % 1000 == 0)
        {
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t_start).count();
            size_t sent = (i + 1) * block_size;
            double throughput_mbps = (static_cast<double>(sent) / 1024.0 / 1024.0) / elapsed;
            std::cout << "[client] sent " << (i + 1) << " blocks, "
                      << sent / 1024.0 / 1024.0 << " MB, "
                      << "throughput: " << throughput_mbps << " MB/s" << std::endl;
        }
    }

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t_start).count();
    size_t total_bytes = iterations * block_size;
    double throughput_mbps = (static_cast<double>(total_bytes) / 1024.0 / 1024.0) / elapsed;

    // shutdown write side so server sees EOF
    asio::error_code ignore;
    sock.shutdown(asio::ip::tcp::socket::shutdown_both, ignore);
    sock.close(ignore);

    std::cout << "[client] done. total " << iterations << " blocks, "
              << total_bytes / 1024.0 / 1024.0 << " MB in " << elapsed << " s, "
              << "throughput: " << throughput_mbps << " MB/s" << std::endl;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        print_usage(argv[0]);

    std::string mode(argv[1]);

    if (mode == "-s")
    {
        // server mode
        if (argc != 5)
            print_usage(argv[0]);
        std::string bind_ip(argv[2]);
        int port = std::stoi(argv[3]);
        size_t block_size = static_cast<size_t>(std::stoull(argv[4]));
        run_server(bind_ip, port, block_size);
    }
    else if (mode == "-c")
    {
        // client mode
        if (argc != 6)
            print_usage(argv[0]);
        std::string server_ip(argv[2]);
        int port = std::stoi(argv[3]);
        size_t block_size = static_cast<size_t>(std::stoull(argv[4]));
        size_t iterations = static_cast<size_t>(std::stoull(argv[5]));
        run_client(server_ip, port, block_size, iterations);
    }
    else
    {
        print_usage(argv[0]);
    }

    return 0;
}
