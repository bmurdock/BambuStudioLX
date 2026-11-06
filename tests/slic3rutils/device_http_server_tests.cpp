#include <catch2/catch.hpp>
#include "slic3r/GUI/DeviceWeb/DeviceHttpServer.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using boost::asio::ip::tcp;

namespace {
struct ServerFixture {
    std::string old_resources = Slic3r::resources_dir();
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("bambu-http-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Slic3r::GUI::DeviceHttpServer server;
    boost::asio::io_context io;
    ServerFixture() {
        std::filesystem::create_directories(root / "web/device_page/dist");
        std::ofstream(root / "web/device_page/dist/index.html") << "device page";
        Slic3r::set_resources_dir(root.u8string());
        server.start();
        REQUIRE(server.is_started());
    }
    ~ServerFixture() {
        server.stop();
        Slic3r::set_resources_dir(old_resources);
        std::filesystem::remove_all(root);
    }
    tcp::socket connect() {
        tcp::socket socket(io);
        socket.connect({boost::asio::ip::address_v4::loopback(), DEVICE_LOCALHOST_PORT});
        return socket;
    }
    std::string response(tcp::socket& socket) {
        std::string result;
        boost::system::error_code ec;
        boost::asio::read(socket, boost::asio::dynamic_buffer(result), ec);
        REQUIRE((!ec || ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset));
        return result;
    }
};
}

TEST_CASE("Device asset server accepts fragmented headers and restarts", "[device_http]")
{
    ServerFixture fixture;
    for (int run = 0; run < 3; ++run) {
        auto socket = fixture.connect();
        const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        socket.set_option(tcp::no_delay(true));
        for (const char c : request) {
            boost::asio::write(socket, boost::asio::buffer(&c, 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto response = fixture.response(socket);
        REQUIRE(response.find("200 OK") != std::string::npos);
        REQUIRE(response.find("device page") != std::string::npos);
        auto unfinished = fixture.connect();
        fixture.server.stop();
        fixture.server.start();
        REQUIRE(fixture.server.is_started());
    }
}

TEST_CASE("Device asset server rejects oversized requests and bounds sessions", "[device_http]")
{
    ServerFixture fixture;
    auto oversized = fixture.connect();
    boost::system::error_code ec;
    const std::string request = "GET /" + std::string(9000, 'x') + " HTTP/1.1\r\n\r\n";
    boost::asio::write(oversized, boost::asio::buffer(request), ec);
    REQUIRE(fixture.response(oversized).empty());

    std::vector<tcp::socket> sockets;
    for (int i = 0; i < 33; ++i) sockets.push_back(fixture.connect());
    auto& excess = sockets.back();
    excess.non_blocking(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    char byte;
    do {
        excess.read_some(boost::asio::buffer(&byte, 1), ec);
        if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    REQUIRE((ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset));
}

TEST_CASE("Device header deadline is absolute despite partial progress", "[device_http]")
{
    ServerFixture fixture;
    auto socket = fixture.connect();
    const auto began = std::chrono::steady_clock::now();
    boost::asio::write(socket, boost::asio::buffer(std::string("GET / HTTP/1.1\r\n")));
    std::this_thread::sleep_for(std::chrono::seconds(6));
    boost::asio::write(socket, boost::asio::buffer(std::string("Host: localhost\r\n")));
    REQUIRE(fixture.response(socket).empty());
    REQUIRE(std::chrono::steady_clock::now() - began < std::chrono::seconds(13));
}
