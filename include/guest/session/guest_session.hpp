#pragma once

#include "guest/capture/window_capture.hpp"
#include "guest/cursor/cursor_watcher.hpp"
#include "guest/input/keyboard_injector.hpp"
#include "guest/input/pointer_injector.hpp"
#include "guest/session/tcp_client.hpp"
#include "guest/windows/window_enumerator.hpp"
#include "guest/windows/window_list_watcher.hpp"
#include "guest/windows/window_drag_watcher.hpp"
#include "guest/windows/window_focus.hpp"
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
    void send_window_list(const std::vector<GuestWindow>& windows);
    void push_window_changes();
    void send_frames();
    void send_window_gone(std::uint64_t window_handle);
    void report_drags();
    void report_cursor();

    TcpClient& link_;
    WindowFocus focus_;
    PointerInjector pointer_{focus_};
    KeyboardInjector keyboard_{focus_};
    WindowResizer resizer_;
    WindowDragWatcher drags_;
    CursorWatcher cursor_;
    FrameDecoder decoder_;
    // Created on the first request, because until the host says which
    // process it wants there is nothing to watch.
    std::unique_ptr<WindowListWatcher> watcher_;
    // One capture per subscribed window: each keeps its own bitmap, sized
    // to that window, across frames.
    std::map<std::uint64_t, std::unique_ptr<WindowCapture>> captures_;
    // Cursor handles the host already has the pixels for.
    std::set<std::uint64_t> sent_cursors_;
    std::vector<std::uint8_t> incoming_;
    std::uint32_t frame_interval_ms_;
    bool running_ = true;
};

}  // namespace rwin::guest
