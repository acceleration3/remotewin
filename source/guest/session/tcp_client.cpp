#include "guest/session/tcp_client.hpp"

#include "guest/capture/frame_signal.hpp"
#include "logging/logger.hpp"

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
    if (readable_ != nullptr) ::WSACloseEvent(static_cast<WSAEVENT>(readable_));
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
        RWIN_ERROR("link", "cannot reach {}:{}: {}", host, port, last_error_);
        ::closesocket(s);
        return false;
    }
    // Frames are latency-sensitive and small control messages must not
    // wait behind Nagle for one.
    const int one = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one),
                 sizeof(one));
    const WSAEVENT readable = ::WSACreateEvent();
    if (readable == WSA_INVALID_EVENT || ::WSAEventSelect(s, readable, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
        fail("WSAEventSelect");
        RWIN_ERROR("link", "cannot watch the socket to {}:{}: {}", host, port, last_error_);
        if (readable != WSA_INVALID_EVENT) ::WSACloseEvent(readable);
        ::closesocket(s);
        return false;
    }
    socket_ = s;
    readable_ = readable;
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

bool TcpClient::wait(std::uint32_t timeout_ms, const FrameSignal& frames) {
    // The socket first: with both set, the host is heard, and the frame's
    // event stays set for the next wait.
    const HANDLE waited[] = {static_cast<HANDLE>(readable_), static_cast<HANDLE>(frames.event())};
    if (::WaitForMultipleObjects(2, waited, FALSE, timeout_ms) != WAIT_OBJECT_0) return false;
    // Resets the event; Winsock sets it again while unread bytes remain.
    WSANETWORKEVENTS happened{};
    ::WSAEnumNetworkEvents(static_cast<SOCKET>(socket_), static_cast<WSAEVENT>(readable_), &happened);
    return true;
}

bool TcpClient::wait_writable() const {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(static_cast<SOCKET>(socket_), &writable);
    return ::select(0, nullptr, &writable, nullptr, nullptr) > 0;
}

bool TcpClient::send(const std::vector<std::uint8_t>& bytes) {
    const auto sequence = ++sends_;
    timing_.note("send_begin", sequence, 0, bytes.size());
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        timing_.note("syscall_begin", sequence);
        const int n = ::send(static_cast<SOCKET>(socket_),
                             reinterpret_cast<const char*>(bytes.data() + sent),
                             static_cast<int>(bytes.size() - sent), 0);
        const int error = n == SOCKET_ERROR ? ::WSAGetLastError() : 0;
        timing_.note("syscall_end", sequence, 0, error);
        if (n == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
            timing_.note("writable_begin", sequence);
            const bool writable = wait_writable();
            timing_.note("writable_end", sequence);
            if (writable) continue;
        }
        if (n <= 0) {
            if (n == SOCKET_ERROR && error != WSAEWOULDBLOCK) ::WSASetLastError(error);
            fail("send");
            RWIN_ERROR("link", "send of {} bytes failed: {}", bytes.size(), last_error_);
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    timing_.note("send_end", sequence, 0, bytes.size());
    return true;
}

bool TcpClient::receive(std::vector<std::uint8_t>& into) {
    const std::size_t at = into.size();
    into.resize(at + kReadChunk);
    const int n = ::recv(static_cast<SOCKET>(socket_),
                         reinterpret_cast<char*>(into.data() + at),
                         static_cast<int>(kReadChunk), 0);
    if (n == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK) {
        into.resize(at);
        return true;
    }
    if (n <= 0) {
        into.resize(at);
        fail("recv");
        RWIN_INFO("link", "host closed the connection: {}", last_error_);
        return false;
    }
    into.resize(at + static_cast<std::size_t>(n));
    RWIN_TRACE("link", "read {} bytes", n);
    return true;
}

}  // namespace rwin::guest
