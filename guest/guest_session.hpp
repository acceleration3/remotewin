#pragma once

#include "guest/input_injector.hpp"
#include "guest/tcp_client.hpp"
#include "guest/window_capture.hpp"
#include "guest/window_enumerator.hpp"
#include "proto/frame_decoder.hpp"

#include <cstdint>
#include <map>
#include <memory>

namespace rwin {
class MessageReader;
}

namespace rwin::guest {

// One connection to the host: answers what it asks, and sends the pixels
// of whatever it subscribed to until it goes away.
class GuestSession {
public:
    GuestSession(TcpClient& link, std::uint32_t frames_per_second);

    // Returns when the host disconnects.
    void run();

private:
    void handle(MessageReader& message);
    void send_window_list(std::uint32_t process_id);
    void send_frames();
    void send_window_gone(std::uint64_t window_handle);

    TcpClient& link_;
    WindowEnumerator windows_;
    InputInjector input_;
    FrameDecoder decoder_;
    // One capture per subscribed window: each keeps its own bitmap, sized
    // to that window, across frames.
    std::map<std::uint64_t, std::unique_ptr<WindowCapture>> captures_;
    std::vector<std::uint8_t> incoming_;
    std::uint32_t frame_interval_ms_;
    bool running_ = true;
};

}  // namespace rwin::guest
