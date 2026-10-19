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
#include "guest/cursor/sent_cursors.hpp"
#include "guest/input/pointer_grab_watcher.hpp"
#include "guest/session/outbox.hpp"
#include "guest/windows/window_directory.hpp"
#include "guest/windows/window_watch.hpp"
#include "proto/feature_set.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/window_id.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace rwin {
class MessageReader;
struct PointerButton;
struct PointerMove;
struct PointerWheel;
}  // namespace rwin

namespace rwin::guest {

class TcpClient;
class WindowSurface;

// One connection to the host (wire 6, docs/rail-model.md §4): the windows
// of the guest's session as they change (WindowWatch), the frames of those
// the host subscribed to or of the whole monitor, the cursor, and the
// host's input and window commands put back into the guest.
//
// Each end says first which features it speaks (Hello); a host that does
// not speak "windows", or says something else first, ends the session
// before a word of it is misread.
class GuestSession {
public:
    GuestSession(TcpClient& link, std::uint32_t frames_per_second);

    // Returns when the host disconnects.
    void run();

private:
    // How often the reader looks up from the socket to see whether the
    // session is still running.
    static constexpr std::uint32_t kReadWaitMs = 100;
    // How soon a window whose capture failed is captured again.
    static constexpr std::chrono::milliseconds kRecaptureAfter{1000};

    using Clock = std::chrono::steady_clock;

    // A window the host subscribed to: its capture, and whether that
    // failed, which is said once and tried again a while later.
    struct Subscription {
        std::unique_ptr<WindowCapture> capture;
        bool lost = false;
        Clock::time_point retry_at{};
    };

    // The reader thread's.
    void read_link();
    void queue(const std::uint8_t* payload, std::size_t size);
    // Input, activation and clock probes, handled where they were read:
    // true when ``message`` was one.
    [[nodiscard]] bool handle_input(MessageReader& message);
    void greet(MessageReader& message);
    void move_pointer(const PointerMove& move);
    void press_button(const PointerButton& press);
    void turn_wheel_over(const PointerWheel& wheel);
    void leave(WindowId window);
    // The main loop's.
    void wait_for_work();
    void serve_queued();
    void handle(MessageReader& message);
    void handle_frames(MessageReader& message);
    void subscribe(WindowId window);
    void unsubscribe(WindowId window);
    void send(const std::vector<std::uint8_t>& bytes);
    void send_frames();
    void send_window_frame(WindowId id, Subscription& subscription);
    [[nodiscard]] CaptureResult send_pixel_frame(WindowId id, WindowCapture& capture, const WindowSurface& surface);
    [[nodiscard]] CaptureResult send_shared_frame(WindowId id, WindowCapture& capture, const WindowSurface& surface);
    void send_pixels(WindowId id, const WindowCapture& capture);
    void send_delivery(const SurfaceRing::Delivery& delivery);
    void send_monitor_frame();
    void send_monitor_shared();
    void send_monitor_pixels();
    void send_monitor_patch();
    void lose_monitor();
    void publish_monitor_origin();
    void take_route(bool shared_surfaces);
    // Frames go as pixels from here on, for every window.
    void stop_sharing();
    // Every capture made afresh, so each sends a frame again however still
    // it sits: the host may not have the ones it was sent.
    void recapture();
    void note_drawn(WindowId id, const WindowCapture& capture, const WindowSurface& surface);
    [[nodiscard]] SurfaceRing& ring_for(WindowId window);
    [[nodiscard]] PictureEdges& edges_for(WindowId window);
    void report_cursor();
    // With shared_ held.
    void report_grab();
    // The foreground window when it is one the host subscribed to, else
    // none. With shared_ held.
    [[nodiscard]] WindowId mirrored_foreground() const;
    // Where the frames of ``window`` start inside its frame: its picture's
    // offset, which input is measured from.
    [[nodiscard]] std::optional<WindowSurface> surface_of(WindowId window, std::int32_t& into_x,
                                                          std::int32_t& into_y) const;

    TcpClient& link_;
    Outbox outbox_;
    GraphicsDevice graphics_;
    // Before every capture that raises it, so it outlives them all.
    FrameSignal frames_;
    // Raised when the main loop has something other than a frame: a
    // message the reader queued, the reader ending, the cursor changing.
    FrameSignal woken_;
    CursorWatcher cursor_{woken_};
    CursorDecoder cursor_pictures_;
    SentCursors sent_cursors_;
    PointerGrabWatcher grabs_;
    FrameDecoder decoder_;
    WindowDirectory directory_;
    FeatureSet features_;
    // Made once the host's hello has said which features it speaks.
    std::unique_ptr<WindowWatch> windows_;
    // Made when the host asks for the whole monitor (WatchMonitor).
    std::unique_ptr<MonitorCapture> monitor_;
    // What of the monitor's picture the host has, so only what changed is sent.
    ChangedTiles monitor_tiles_;
    std::map<std::uint64_t, Subscription> subscriptions_;
    // Made when the host first says it takes shared surfaces; one per
    // process, as the kernel device it opens is.
    std::unique_ptr<SurfaceIdReader> surface_ids_;
    bool shared_route_ = false;
    std::uint32_t rings_made_ = 0;
    // One per window whose frames stay on the GPU, and one for the monitor.
    std::map<std::uint64_t, std::unique_ptr<SurfaceRing>> rings_;
    // Windows the host cannot draw from shared surfaces (WindowPixels).
    std::set<std::uint64_t> pixels_only_;
    // Whether a frame of a window that may leave a margin round itself
    // draws out to its edges: one per window.
    std::map<std::uint64_t, std::unique_ptr<PictureEdges>> edges_;
    // What the reader handed the main loop, and what the main loop is
    // working through.
    std::mutex inbox_lock_;
    std::vector<std::vector<std::uint8_t>> inbox_;
    std::vector<std::vector<std::uint8_t>> serving_;
    // What input, read on the reader thread, shares with the main loop:
    // grabs_, subscribed_ and monitor_at_. Held briefly on both.
    mutable std::mutex shared_;
    std::set<std::uint64_t> subscribed_;
    struct ScreenPoint {
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    // The captured monitor's top left on the guest's screen, once known.
    std::optional<ScreenPoint> monitor_at_;
    // The longest the session sleeps: frames wake it as they are composed,
    // and this is how often the cursor and the grab are looked at when
    // nothing does.
    std::uint32_t frame_interval_ms_;
    std::atomic<bool> running_{true};
    // The reader's.
    bool greeted_ = false;
    FrameTiming timing_{"guest"};
};

}  // namespace rwin::guest
