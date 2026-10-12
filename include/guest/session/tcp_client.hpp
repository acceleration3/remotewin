#pragma once

#include "logging/frame_timing.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rwin::guest {

// The socket to the host, owned.
//
// The guest dials out rather than listening: QEMU's user-mode networking
// gives the guest the host at a fixed address with no forwarding rule,
// while reaching a guest port needs one declared before the VM boots.
class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&&) = delete;
    TcpClient& operator=(TcpClient&&) = delete;

    [[nodiscard]] bool connect(const std::string& host, std::uint16_t port);

    // From any thread: each message goes whole, one after another.
    [[nodiscard]] bool send(const std::vector<std::uint8_t>& bytes);

    // Until the host has said something, or ``timeout_ms`` passed; true
    // for the first. One thread waits here: the event it waits on is reset
    // by the wait.
    [[nodiscard]] bool wait_readable(std::uint32_t timeout_ms) const;

    // Bytes as they arrive, appended to ``into``. False once the peer is
    // gone, which is how the session learns to exit.
    [[nodiscard]] bool receive(std::vector<std::uint8_t>& into);

    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    void fail(const char* what);
    // Until a send would take something; false when the socket failed.
    [[nodiscard]] bool wait_writable() const;

    std::uintptr_t socket_;
    // Set by Winsock while the socket has something to read, or closed.
    // Selecting it makes the socket non-blocking, so send waits out a full
    // buffer itself.
    void* readable_ = nullptr;
    std::string last_error_;
    std::mutex sending_;
    FrameTiming timing_{"socket"};
    std::uint64_t sends_ = 0;
};

}  // namespace rwin::guest
