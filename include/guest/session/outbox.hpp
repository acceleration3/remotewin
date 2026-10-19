#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace rwin::guest {

class TcpClient;

// Where every thread of the guest half puts what it says to the host: one
// message at a time, whole, and once one fails, the session is over.
class Outbox {
public:
    explicit Outbox(TcpClient& link) noexcept : link_(link) {}

    void send(const std::vector<std::uint8_t>& message);
    [[nodiscard]] bool failed() const noexcept { return failed_.load(); }

private:
    TcpClient& link_;
    std::atomic<bool> failed_{false};
};

}  // namespace rwin::guest
