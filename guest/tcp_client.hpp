#pragma once

#include <cstdint>
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

    [[nodiscard]] bool send(const std::vector<std::uint8_t>& bytes);

    // True when a read would not block. The timeout is what paces the
    // session: it waits for a command only until the next frame is due.
    [[nodiscard]] bool wait_readable(std::uint32_t timeout_ms) const;

    // Bytes as they arrive, appended to ``into``. False once the peer is
    // gone, which is how the session learns to exit.
    [[nodiscard]] bool receive(std::vector<std::uint8_t>& into);

    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    void fail(const char* what);

    std::uintptr_t socket_;
    std::string last_error_;
};

}  // namespace rwin::guest
