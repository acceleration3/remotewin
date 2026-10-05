#include "probe/command_reader.hpp"

#include "guest/session/tcp_client.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

namespace rwin::probe {

namespace {
// How long a wait for the socket lasts before the thread looks whether it
// is being stopped.
constexpr std::uint32_t kStopCheckMs = 100;
}  // namespace

CommandReader::CommandReader(guest::TcpClient& link, HWND__* notify, unsigned arrived_message)
    : link_(link), notify_(notify), arrived_message_(arrived_message) {
    thread_ = std::thread([this] { read(); });
}

CommandReader::~CommandReader() {
    stopping_.store(true);
    if (thread_.joinable()) thread_.join();
}

std::optional<std::vector<std::uint8_t>> CommandReader::next() {
    const std::lock_guard lock{mutex_};
    if (commands_.empty()) return std::nullopt;
    std::vector<std::uint8_t> command = std::move(commands_.front());
    commands_.pop_front();
    return command;
}

void CommandReader::read() {
    std::vector<std::uint8_t> incoming;
    while (!stopping_.load()) {
        if (!link_.wait_readable(kStopCheckMs)) continue;
        if (!link_.receive(incoming)) break;
        decoder_.feed(incoming.data(), incoming.size());
        incoming.clear();
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        bool any = false;
        {
            const std::lock_guard lock{mutex_};
            while (decoder_.next(&payload, &size)) {
                commands_.emplace_back(payload, payload + size);
                any = true;
            }
        }
        if (decoder_.overflowed()) {
            RWIN_ERROR("session", "the harness's framing is broken; leaving");
            break;
        }
        if (any) ::PostMessageW(notify_, arrived_message_, 0, 0);
    }
    finished_.store(true);
    ::PostMessageW(notify_, arrived_message_, 0, 0);
}

}  // namespace rwin::probe
