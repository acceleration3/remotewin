#pragma once

#include "logging/frame_timing.hpp"

#include "guest/capture/capture_closer.hpp"
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
#include "guest/input/input_target.hpp"
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
#include <utility>
#include <vector>

namespace rwin {
class MessageReader;
struct PointerButton;
struct PointerMove;
struct PointerWheel;
}

namespace rwin::guest {

class TcpClient;
class WindowSurface;

class GuestSession {
public:
    GuestSession(TcpClient& link, std::uint32_t frames_per_second);

    void run();

private:
    static constexpr std::uint32_t kReadWaitMs = 100;
    static constexpr std::chrono::milliseconds kRecaptureAfter{1000};

    using Clock = std::chrono::steady_clock;

    struct Subscription {
        std::unique_ptr<WindowCapture> capture;
        bool lost = false;
        bool unseen = false;
        Clock::time_point retry_at{};
    };

    void read_link();
    void queue(const std::uint8_t* payload, std::size_t size);
    [[nodiscard]] bool handle_input(MessageReader& message);
    void greet(MessageReader& message);
    void move_pointer(const PointerMove& move);
    void press_button(const PointerButton& press);
    void turn_wheel_over(const PointerWheel& wheel);
    void leave(WindowId window);
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
    void stop_sharing();
    void recapture();
    void recapture(WindowId window);
    void note_drawn(WindowId id, const WindowCapture& capture, const WindowSurface& surface);
    [[nodiscard]] SurfaceRing& ring_for(WindowId window);
    [[nodiscard]] PictureEdges& edges_for(WindowId window);
    void report_cursor();
    void report_grab();
    void report_cursor_at();
    [[nodiscard]] WindowId mirrored_foreground() const;
    [[nodiscard]] std::optional<WindowSurface> surface_of(WindowId window, std::int32_t& into_x,
                                                          std::int32_t& into_y) const;

    TcpClient& link_;
    Outbox outbox_;
    GraphicsDevice graphics_;
    FrameSignal frames_;
    CaptureCloser closer_;
    FrameSignal woken_;
    CursorWatcher cursor_{woken_};
    CursorDecoder cursor_pictures_;
    SentCursors sent_cursors_;
    PointerGrabWatcher grabs_;
    std::optional<std::pair<std::int32_t, std::int32_t>> cursor_sent_at_;
    InputTarget target_;
    FrameDecoder decoder_;
    WindowDirectory directory_;
    FeatureSet features_;
    std::unique_ptr<WindowWatch> windows_;
    std::unique_ptr<MonitorCapture> monitor_;
    ChangedTiles monitor_tiles_;
    std::map<std::uint64_t, Subscription> subscriptions_;
    std::unique_ptr<SurfaceIdReader> surface_ids_;
    bool shared_route_ = false;
    std::uint32_t rings_made_ = 0;
    std::map<std::uint64_t, std::unique_ptr<SurfaceRing>> rings_;
    std::set<std::uint64_t> pixels_only_;
    std::map<std::uint64_t, std::unique_ptr<PictureEdges>> edges_;
    std::mutex inbox_lock_;
    std::vector<std::vector<std::uint8_t>> inbox_;
    std::vector<std::vector<std::uint8_t>> serving_;
    mutable std::mutex shared_;
    std::set<std::uint64_t> subscribed_;
    struct ScreenPoint {
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    std::optional<ScreenPoint> monitor_at_;
    std::uint32_t frame_interval_ms_;
    std::atomic<bool> running_{true};
    bool greeted_ = false;
    FrameTiming timing_{"guest"};
};

}
