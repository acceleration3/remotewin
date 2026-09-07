#pragma once

#include "guest/capture/graphics_device.hpp"
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
#include "guest/windows/desktop_stand_in.hpp"
#include "guest/windows/window_state.hpp"
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
    GraphicsDevice graphics_;
    WindowFocus focus_;
    PointerInjector pointer_{focus_};
    KeyboardInjector keyboard_{focus_};
    WindowResizer resizer_;
    DesktopStandIn elsewhere_;
    WindowState state_{elsewhere_};
    WindowDragWatcher drags_;
    CursorWatcher cursor_;
    FrameDecoder decoder_;
    // Created when the host says what to watch; until then there is no
    // application to have windows.
    std::unique_ptr<WindowListWatcher> watcher_;
    // One capture per subscribed window: each keeps its own bitmap, sized
    // to that window, across frames.
    std::map<std::uint64_t, std::unique_ptr<WindowCapture>> captures_;
    // What each window turned out to actually draw, learned from its
    // last capture. The window list is reported through it, so the size
    // and position the host is given are the size and position of the
    // pixels it receives — a window that reserves a shadow margin is
    // otherwise reported larger than anything it ever fills.
    struct DrawnArea {
        std::uint32_t offset_x = 0;
        std::uint32_t offset_y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    std::map<std::uint64_t, DrawnArea> drawn_;

    // Cursor handles the host already has the pixels for.
    std::set<std::uint64_t> sent_cursors_;
    std::vector<std::uint8_t> incoming_;
    std::uint32_t frame_interval_ms_;
    bool running_ = true;
};

}  // namespace rwin::guest
