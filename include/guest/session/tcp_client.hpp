#pragma once

#include "logging/frame_timing.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rwin::guest {
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

    [[nodiscard]] bool wait_readable(std::uint32_t timeout_ms) const;

    [[nodiscard]] bool receive(std::vector<std::uint8_t>& into);

    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    void fail(const char* what);
    [[nodiscard]] bool wait_writable() const;

    std::uintptr_t socket_;
    void* readable_ = nullptr;
    std::string last_error_;
    std::mutex sending_;
    FrameTiming timing_{"socket"};
    std::uint64_t sends_ = 0;
};

}
