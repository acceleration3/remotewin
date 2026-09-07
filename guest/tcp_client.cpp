#include "guest/tcp_client.hpp"

#include "log/logger.hpp"

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
// clang-format on

namespace rwin::guest {

namespace {
constexpr std::size_t kReadChunk = 64 * 1024;
}

TcpClient::TcpClient() : socket_(INVALID_SOCKET) {
    WSADATA data{};
    ::WSAStartup(MAKEWORD(2, 2), &data);
}

TcpClient::~TcpClient() {
    if (socket_ != INVALID_SOCKET) {
        ::closesocket(static_cast<SOCKET>(socket_));
    }
    ::WSACleanup();
}

void TcpClient::fail(const char* what) {
    last_error_ = std::string{what} + " (wsa " + std::to_string(::WSAGetLastError()) + ")";
}

bool TcpClient::connect(const std::string& host, std::uint16_t port) {
    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        fail("socket");
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = ::htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::closesocket(s);
        last_error_ = "not an IPv4 address: " + host;
        return false;
    }
    RWIN_DEBUG("link", "dialling {}:{}", host, port);
    if (::connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        fail("connect");
        RWIN_ERROR("link", "cannot reach {}:{} — {}", host, port, last_error_);
        ::closesocket(s);
        return false;
    }
    // Frames are latency-sensitive and small control messages must not
    // wait behind Nagle for one.
    const int one = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one),
                 sizeof(one));
    socket_ = s;
    RWIN_INFO("link", "connected to {}:{}", host, port);
    return true;
}

bool TcpClient::wait_readable(std::uint32_t timeout_ms) const {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(static_cast<SOCKET>(socket_), &readable);
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1000);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
    return ::select(0, &readable, nullptr, nullptr, &timeout) > 0;
}

bool TcpClient::send(const std::vector<std::uint8_t>& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const int n = ::send(static_cast<SOCKET>(socket_),
                             reinterpret_cast<const char*>(bytes.data() + sent),
                             static_cast<int>(bytes.size() - sent), 0);
        if (n <= 0) {
            fail("send");
            RWIN_ERROR("link", "send of {} bytes failed — {}", bytes.size(), last_error_);
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool TcpClient::receive(std::vector<std::uint8_t>& into) {
    const std::size_t at = into.size();
    into.resize(at + kReadChunk);
    const int n = ::recv(static_cast<SOCKET>(socket_),
                         reinterpret_cast<char*>(into.data() + at),
                         static_cast<int>(kReadChunk), 0);
    if (n <= 0) {
        into.resize(at);
        fail("recv");
        RWIN_INFO("link", "host closed the connection — {}", last_error_);
        return false;
    }
    into.resize(at + static_cast<std::size_t>(n));
    RWIN_TRACE("link", "read {} bytes", n);
    return true;
}

}  // namespace rwin::guest
