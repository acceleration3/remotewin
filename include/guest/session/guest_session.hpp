#pragma once

#include "logging/frame_timing.hpp"

#include "guest/capture/changed_tiles.hpp"
#include "guest/capture/frame_signal.hpp"
#include "guest/capture/graphics_device.hpp"
#include "guest/capture/monitor_capture.hpp"
#include "guest/capture/picture_edges.hpp"
#include "guest/capture/surface_id_reader.hpp"
#include "guest/capture/surface_ring.hpp"
#include "guest/capture/window_capture.hpp"
#include "guest/cursor/cursor_decoder.hpp"
#include "guest/cursor/cursor_watcher.hpp"
#include "guest/input/keyboard_injector.hpp"
#include "guest/input/pointer_grab_watcher.hpp"
#include "guest/input/pointer_injector.hpp"
#include "guest/session/tcp_client.hpp"
#include "guest/windows/window_enumerator.hpp"
#include "guest/windows/window_list_watcher.hpp"
#include "guest/windows/window_drag_watcher.hpp"
#include "guest/windows/window_focus.hpp"
#include "guest/windows/window_mover.hpp"
#include "guest/windows/shadow_setting.hpp"
#include "guest/windows/window_resizer.hpp"
#include "guest/windows/work_area.hpp"
#include "proto/frame_decoder.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace rwin {
class MessageReader;
}

namespace rwin {
struct PointerWheel;
}

namespace rwin::guest {

class WindowSurface;

// One connection to the host: answers what it asks, and sends the pixels,
// the hit zones and the cursors of whatever it subscribed to until it
// goes away - an application's windows, or the whole monitor.
//
// Each end says first which wire it speaks (WireHello); a host that says
// another, or says nothing first, ends the session before a word of it is
// misread.
class GuestSession {
public:
    GuestSession(TcpClient& link, std::uint32_t frames_per_second);

    // Returns when the host disconnects.
    void run();

private:
    void say_hello();
    // The host's hello, which has to be the first thing it sends.
    void greet(MessageReader& message);
    void handle(MessageReader& message);
    void move_pointer(std::uint64_t window, std::int32_t x, std::int32_t y);
    void press_pointer(MessageReader& message);
    void send_window_list(const std::vector<GuestWindow>& windows);
    void push_window_changes();
    void send_frames();
    [[nodiscard]] CaptureResult send_pixel_frame(std::uint64_t handle, WindowCapture& capture,
                                                 const WindowSurface& surface);
    [[nodiscard]] CaptureResult send_shared_frame(std::uint64_t handle, WindowCapture& capture,
                                                  const WindowSurface& surface);
    void send_pixels(std::uint64_t handle, const WindowCapture& capture);
    void send_delivery(const SurfaceRing::Delivery& delivery);
    void send_monitor_frame();
    void send_monitor_shared();
    void send_monitor_pixels();
    void send_monitor_patch();
    void lose_monitor();
    // The host's FrameRoute.
    void take_route(bool shared_surfaces);
    // Frames go as pixels from here on, for every window.
    void stop_sharing();
    // Every capture made afresh, so each sends a frame again however still
    // it sits: the host may not have the ones it was sent.
    void recapture();
    [[nodiscard]] SurfaceRing& ring_for(std::uint64_t window);
    void send_window_gone(std::uint64_t window_handle);
    void turn_wheel(const PointerWheel& wheel);
    void report_drags();
    void report_cursor();
    void report_grab();
    // The foreground window when it is one of the mirrored application's
    // (one subscribed to), else 0.
    [[nodiscard]] std::uint64_t mirrored_foreground() const;

    TcpClient& link_;
    GraphicsDevice graphics_;
    // Before every capture that raises it, so it outlives them all.
    FrameSignal frames_;
    WindowFocus focus_;
    PointerInjector pointer_{focus_};
    KeyboardInjector keyboard_{focus_};
    WindowResizer resizer_;
    WindowMover mover_;
    WorkArea room_;
    // Created with watcher_: a drag is handed to the host only where the
    // host's compositor holds the window. Mirroring the whole monitor,
    // Windows drags its own windows inside the picture.
    std::unique_ptr<WindowDragWatcher> drags_;
    CursorWatcher cursor_;
    CursorDecoder cursor_pictures_;
    PointerGrabWatcher grabs_;
    ShadowSetting shadows_;
    FrameDecoder decoder_;
    // Created when the host says what to watch; until then there is no
    // application to have windows.
    std::unique_ptr<WindowListWatcher> watcher_;
    // What watcher_ was made for.
    std::uint32_t watched_process_ = 0;
    std::string watched_job_;
    // Created when the host asks for the whole monitor (WatchMonitor).
    std::unique_ptr<MonitorCapture> monitor_;
    // What of the monitor's picture the host has, so only what changed is sent.
    ChangedTiles monitor_tiles_;
    // One capture per subscribed window: each keeps its own bitmap, sized
    // to that window, across frames.
    std::map<std::uint64_t, std::unique_ptr<WindowCapture>> captures_;
    // What each window turned out to actually draw, learned from its
    // last capture. The window list is reported through it, so the size
    // and position the host is given are the size and position of the
    // pixels it receives — a window that reserves a shadow margin is
    // otherwise reported larger than anything it ever fills.
    // Where in the picture (WindowSurface) the host's coordinates start:
    // the host has the part of it that draws something, which is all of it
    // unless the window leaves a transparent margin round itself.
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> picture_offset(
        std::uint64_t window_handle) const;

    struct DrawnArea {
        std::uint32_t offset_x = 0;
        std::uint32_t offset_y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        [[nodiscard]] bool operator==(const DrawnArea&) const noexcept = default;
    };
    std::map<std::uint64_t, DrawnArea> drawn_;
    void note_drawn(std::uint64_t window_handle, const DrawnArea& area);

    // Made when the host first says it takes shared surfaces; one per
    // process, as the kernel device it opens is.
    std::unique_ptr<SurfaceIdReader> surface_ids_;
    bool shared_route_ = false;
    std::uint32_t rings_made_ = 0;
    // One per window whose frames stay on the GPU, and one for the monitor
    // (kMonitorWindow).
    std::map<std::uint64_t, std::unique_ptr<SurfaceRing>> rings_;
    // Whether a frame of a window that may leave a margin round itself
    // draws out to its edges, and so needs no trimming.
    PictureEdges edges_{graphics_};

    // Cursor handles the host already has the pixels for.
    std::set<std::uint64_t> sent_cursors_;
    std::vector<std::uint8_t> incoming_;
    // The longest the session sleeps: frames wake it as they are composed,
    // and this is how often windows, the cursor and the grab are looked at
    // when nothing does.
    std::uint32_t frame_interval_ms_;
    bool running_ = true;
    bool greeted_ = false;
    FrameTiming timing_{"guest"};
};

}  // namespace rwin::guest
