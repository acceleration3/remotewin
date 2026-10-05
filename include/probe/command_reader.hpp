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

// Reads the harness's commands on a thread of its own, and tells the
// probe's window thread each time some have arrived by posting it a
// message.
//
// Because a message is what any message loop on that thread dispatches:
// a modal dialog's, a message box's, a menu's. A probe that read its
// socket between messages stopped answering the moment it opened a modal
// dialog, since Windows' modal loop runs until the dialog ends and the
// harness was the one to end it.
class CommandReader {
public:
    CommandReader(guest::TcpClient& link, HWND__* notify, unsigned arrived_message);
    ~CommandReader();

    CommandReader(const CommandReader&) = delete;
    CommandReader& operator=(const CommandReader&) = delete;
    CommandReader(CommandReader&&) = delete;
    CommandReader& operator=(CommandReader&&) = delete;

    // The oldest command not yet taken, as its whole payload.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> next();

    // The harness went away or broke its framing; nothing more will come.
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

}  // namespace rwin::probe
