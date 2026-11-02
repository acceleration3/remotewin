#pragma once

#include "proto/frame_decoder.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

struct HWND__;

namespace rwin::guest {
class TcpClient;
}

namespace rwin::probe {
class CommandReader {
public:
    CommandReader(guest::TcpClient& link, HWND__* notify, unsigned arrived_message);
    ~CommandReader();

    CommandReader(const CommandReader&) = delete;
    CommandReader& operator=(const CommandReader&) = delete;
    CommandReader(CommandReader&&) = delete;
    CommandReader& operator=(CommandReader&&) = delete;

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> next();

    [[nodiscard]] bool finished() const noexcept { return finished_.load(); }

private:
    void read();

    guest::TcpClient& link_;
    HWND__* notify_;
    unsigned arrived_message_;
    FrameDecoder decoder_;
    std::mutex mutex_;
    std::deque<std::vector<std::uint8_t>> commands_;
    std::atomic<bool> finished_{false};
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}
