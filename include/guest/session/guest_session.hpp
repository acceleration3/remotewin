#pragma once

#include "guest/capture/window_capture.hpp"
#include "guest/cursor/cursor_watcher.hpp"
#include "guest/input/keyboard_injector.hpp"
#include "guest/input/pointer_injector.hpp"
#include "guest/session/tcp_client.hpp"
#include "guest/windows/window_enumerator.hpp"
#include "guest/windows/window_focus.hpp"
#include "guest/windows/window_hit_test.hpp"
#include "guest/windows/window_resizer.hpp"
#include "proto/frame_decoder.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace rwin {
class MessageReader;
}

namespace rwin::guest {

// One connection to the host: answers what it asks, and sends the pixels,
// the hit zones and the cursors of whatever it subscribed to until it
// goes away.
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
    void report_hit(std::uint64_t window_handle, std::int32_t x, std::int32_t y);
    void report_cursor();

    TcpClient& link_;
    WindowEnumerator windows_;
    WindowFocus focus_;
    PointerInjector pointer_{focus_};
    KeyboardInjector keyboard_{focus_};
    WindowHitTest hit_test_;
    WindowResizer resizer_;
    CursorWatcher cursor_;
    FrameDecoder decoder_;
    // One capture per subscribed window: each keeps its own bitmap, sized
    // to that window, across frames.
    std::map<std::uint64_t, std::unique_ptr<WindowCapture>> captures_;
    // The zone the pointer was last in, per window, so an unchanged answer
    // is not sent thirty times a second.
    std::map<std::uint64_t, std::int32_t> last_hit_;
    // Cursor handles the host already has the pixels for.
    std::set<std::uint64_t> sent_cursors_;
    std::vector<std::uint8_t> incoming_;
    std::uint32_t frame_interval_ms_;
    bool running_ = true;
};

}  // namespace rwin::guest
