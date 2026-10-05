#include "guest/session/guest_session.hpp"

#include <optional>
#include <string>
#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"
#include "proto/pointer_wheel.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/frame_route.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/guest_desktop.hpp"
#include "proto/key_stroke.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/pointer_delta.hpp"
#include "proto/picture_rect.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/slot_free.hpp"
#include "proto/slot_ring.hpp"
#include "proto/wire_hello.hpp"

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

namespace rwin::guest {

GuestSession::GuestSession(TcpClient& link, std::uint32_t frames_per_second)
    : link_(link),
      frame_interval_ms_(1000 / (frames_per_second != 0 ? frames_per_second : 1)) {}

void GuestSession::run() {
    RWIN_INFO("session", "serving; frame interval {} ms, wire {}", frame_interval_ms_,
              WireHello::kVersion);
    say_hello();
    while (running_) {
        if (link_.wait(frame_interval_ms_, frames_)) {
            if (!link_.receive(incoming_)) return;
            decoder_.feed(incoming_.data(), incoming_.size());
            incoming_.clear();

            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            while (running_ && decoder_.next(&payload, &size)) {
                MessageReader message{payload, size};
                if (greeted_) {
                    handle(message);
                } else {
                    greet(message);
                }
            }
            if (decoder_.overflowed()) return;
        }
        // Said with every window list; a change of it is a list to say again.
        if (shadows_.changed() && watcher_ != nullptr) watcher_->forget();
        push_window_changes();
        report_drags();
        send_frames();
        report_cursor();
        report_grab();
    }
}

void GuestSession::say_hello() {
    MessageWriter out{MessageId::GuestHello};
    WireHello{}.write(out);
    running_ = link_.send(out.take());
}

void GuestSession::greet(MessageReader& message) {
    if (message.id() != MessageId::HostHello) {
        RWIN_ERROR("link", "the host's first message is {}, not its hello: it is a remotewin older "
                   "than this guest half (wire {}). Update the host, or put the guest half that "
                   "came with it here", static_cast<int>(message.id()), WireHello::kVersion);
        running_ = false;
        return;
    }
    const std::optional<WireHello> hello = WireHello::read(message);
    if (!hello.has_value() || hello->version != WireHello::kVersion) {
        RWIN_ERROR("link", "the host speaks wire {}, this guest half wire {}: the two halves are "
                   "from different builds. Put the guest half that came with the host here",
                   hello.has_value() ? hello->version : 0, WireHello::kVersion);
        running_ = false;
        return;
    }
    RWIN_INFO("link", "the host speaks wire {} too", hello->version);
    greeted_ = true;
}

void GuestSession::handle(MessageReader& message) {
    switch (message.id()) {
        case MessageId::WatchScope: {
            const std::uint32_t process_id = message.u32();
            const std::string job = message.string();
            if (!message.ok()) break;
            // The host says it again every second; the same scope is the
            // watcher already running, which remembers which processes
            // joined the application since (LaunchScope).
            if (watcher_ == nullptr || process_id != watched_process_ || job != watched_job_) {
                RWIN_INFO("scope", "asked to watch pid {} job '{}'", process_id, job);
                watcher_ = std::make_unique<WindowListWatcher>(job, process_id);
                watched_process_ = process_id;
                watched_job_ = job;
            }
            if (drags_ == nullptr) drags_ = std::make_unique<WindowDragWatcher>();
            push_window_changes();
            break;
        }
        case MessageId::WatchMonitor:
            // Said once per connection; a second is the same request.
            if (monitor_ != nullptr) break;
            RWIN_INFO("scope", "asked to mirror the whole monitor");
            monitor_ = std::make_unique<MonitorCapture>(graphics_, frames_);
            monitor_tiles_ = ChangedTiles{};
            break;
        case MessageId::FrameRoute: {
            const std::optional<FrameRoute> route = FrameRoute::read(message);
            if (!route.has_value()) {
                RWIN_WARN("shared", "malformed frame route");
                break;
            }
            take_route(route->shared_surfaces);
            break;
        }
        case MessageId::SlotFree: {
            const std::optional<SlotFree> freed = SlotFree::read(message);
            if (!freed.has_value()) {
                RWIN_WARN("shared", "malformed slot free");
                break;
            }
            if (const auto ring = rings_.find(freed->window); ring != rings_.end()) ring->second->free(*freed);
            break;
        }
        case MessageId::Subscribe: {
            const std::uint64_t window = message.u64();
            if (!message.ok()) break;
            captures_.try_emplace(window, std::make_unique<WindowCapture>(graphics_, frames_));
            RWIN_INFO("session", "subscribed to window {:#x} ({} now)", window,
                      captures_.size());
            break;
        }
        case MessageId::Unsubscribe: {
            const std::uint64_t window = message.u64();
            captures_.erase(window);
            drawn_.erase(window);
            rings_.erase(window);
            RWIN_INFO("session", "unsubscribed from window {:#x}", window);
            break;
        }
        case MessageId::PointerMove: {
            const std::uint64_t window = message.u64();
            const std::int32_t x = message.i32();
            const std::int32_t y = message.i32();
            if (message.ok()) move_pointer(window, x, y);
            break;
        }
        case MessageId::PointerDelta: {
            const std::optional<PointerDelta> delta = PointerDelta::read(message);
            if (!delta.has_value()) {
                RWIN_WARN("input", "malformed pointer delta");
                break;
            }
            RWIN_TRACE("input", "pointer {:#x} moved by {},{}", delta->window, delta->dx, delta->dy);
            report_grab();
            grabs_.nudging();
            PointerInjector::nudge(delta->dx, delta->dy);
            break;
        }
        case MessageId::PointerButton:
            press_pointer(message);
            break;
        case MessageId::PointerWheel: {
            const std::optional<PointerWheel> wheel = PointerWheel::read(message);
            if (!wheel.has_value()) {
                RWIN_WARN("input", "malformed wheel");
                break;
            }
            turn_wheel(*wheel);
            break;
        }
        case MessageId::Key: {
            const std::optional<KeyStroke> stroke = KeyStroke::read(message);
            if (!stroke.has_value()) {
                RWIN_WARN("input", "malformed key message");
                break;
            }
            RWIN_DEBUG("input", "key {} scan={:#06x} vk={:#x} char={:#x}",
                       stroke->pressed ? "down" : "up", stroke->scan_code,
                       stroke->virtual_key, stroke->character);
            keyboard_.put(*stroke);
            break;
        }
        case MessageId::FocusWindow: {
            const std::uint64_t window = message.u64();
            if (message.ok()) focus_.give_to(window);
            break;
        }
        case MessageId::CloseWindow: {
            const std::uint64_t window = message.u64();
            if (!message.ok()) break;
            RWIN_INFO("session", "closing window {:#x}", window);
            ::PostMessageW(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window)),
                           WM_CLOSE, 0, 0);
            break;
        }
        case MessageId::MoveWindow: {
            const std::uint64_t window = message.u64();
            const std::int32_t x = message.i32();
            const std::int32_t y = message.i32();
            if (!message.ok()) break;
            // The host's corner is the drawn part's, which is the picture's
            // less the margin a window reserves round itself.
            const auto [into_x, into_y] = picture_offset(window);
            mover_.move(WindowSurface{window}, x - into_x, y - into_y);
            break;
        }
        case MessageId::ResizeWindow: {
            const std::uint64_t window = message.u64();
            const std::uint32_t width = message.u32();
            const std::uint32_t height = message.u32();
            if (message.ok()) resizer_.resize(window, width, height);
            break;
        }
        default:
            RWIN_WARN("session", "unknown message id {}",
                      static_cast<int>(message.id()));
            break;
    }
}

void GuestSession::move_pointer(std::uint64_t window, std::int32_t x, std::int32_t y) {
    if (window == kMonitorWindow) {
        if (monitor_ != nullptr) PointerInjector::move_on_screen(monitor_->screen_x(x), monitor_->screen_y(y));
        return;
    }
    const auto [into_x, into_y] = picture_offset(window);
    // What became of the last move is weighed before this one is made: the
    // evidence of mouse look (PointerGrabRule).
    report_grab();
    if (const auto at = pointer_.move_to(WindowSurface{window}, x + into_x, y + into_y)) {
        grabs_.put_at(at->first, at->second);
    }
}

void GuestSession::press_pointer(MessageReader& message) {
    const std::uint64_t window = message.u64();
    const std::int32_t x = message.i32();
    const std::int32_t y = message.i32();
    const auto button = static_cast<PointerInjector::Button>(message.u8());
    const bool pressed = message.u8() != 0;
    if (!message.ok()) return;
    if (window == kMonitorWindow) {
        if (monitor_ != nullptr) {
            PointerInjector::press_on_screen(monitor_->screen_x(x), monitor_->screen_y(y), button, pressed);
        }
        return;
    }
    if (grabs_.current().kind == PointerGrab::Kind::Lock) {
        pointer_.press_in_place(WindowSurface{window}, button, pressed);
        return;
    }
    const auto [into_x, into_y] = picture_offset(window);
    pointer_.press(WindowSurface{window}, x + into_x, y + into_y, button, pressed);
}

void GuestSession::turn_wheel(const PointerWheel& wheel) {
    if (wheel.window == kMonitorWindow) {
        if (monitor_ != nullptr) {
            PointerInjector::turn_on_screen(monitor_->screen_x(wheel.x), monitor_->screen_y(wheel.y), wheel.dx,
                                            wheel.dy);
        }
        return;
    }
    if (grabs_.current().kind == PointerGrab::Kind::Lock) {
        PointerInjector::turn_in_place(wheel.dx, wheel.dy);
        return;
    }
    const auto [into_x, into_y] = picture_offset(wheel.window);
    pointer_.turn(WindowSurface{wheel.window}, wheel.x + into_x, wheel.y + into_y, wheel.dx, wheel.dy);
}

void GuestSession::report_drags() {
    if (drags_ == nullptr) return;
    for (const std::uint64_t window : drags_->drained()) {
        MessageWriter out{MessageId::DragStarted};
        out.put_u64(window);
        if (!link_.send(out.take())) {
            running_ = false;
            return;
        }
    }
}

void GuestSession::report_cursor() {
    const std::optional<GuestCursor> cursor = cursor_.changed();
    if (!cursor.has_value()) return;

    if (cursor->shown && !sent_cursors_.contains(cursor->id)) {
        const std::optional<CursorPicture> picture =
            cursor_pictures_.decode(reinterpret_cast<void*>(static_cast<std::uintptr_t>(cursor->id)));
        if (!picture.has_value()) {
            RWIN_WARN("cursor", "cursor {:#x} could not be decoded", cursor->id);
            return;
        }
        const CursorPicture::Frame& first = picture->frames.front();
        RWIN_DEBUG("cursor", "new cursor {:#x} {}: {} picture(s) {}x{} hotspot {},{} delay {} ms{}", cursor->id,
                   name_of(picture->system), picture->frames.size(), first.width, first.height, first.hotspot_x,
                   first.hotspot_y, first.delay_ms, picture->inverts ? ", inverting" : "");
        MessageWriter shape{MessageId::CursorShape};
        picture->write(shape);
        if (!link_.send(shape.take())) {
            running_ = false;
            return;
        }
        sent_cursors_.insert(cursor->id);
    }

    RWIN_DEBUG("cursor", "cursor now {:#x} {}", cursor->id, cursor->shown ? "shown" : "hidden");
    MessageWriter changed{MessageId::CursorChanged};
    cursor->write(changed);
    running_ = link_.send(changed.take());
}

std::uint64_t GuestSession::mirrored_foreground() const {
    const auto foreground = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(::GetForegroundWindow()));
    return captures_.contains(foreground) ? foreground : 0;
}

void GuestSession::report_grab() {
    const std::optional<PointerGrab> grab = grabs_.changed(mirrored_foreground());
    if (!grab.has_value()) return;
    MessageWriter out{MessageId::PointerGrabbed};
    grab->write(out);
    running_ = link_.send(out.take());
}

void GuestSession::push_window_changes() {
    if (watcher_ == nullptr) return;
    std::optional<std::vector<GuestWindow>> changed = watcher_->changed();
    if (!changed.has_value()) return;
    RWIN_DEBUG("windows", "{} window(s)", changed->size());
    for (const GuestWindow& window : *changed) {
        RWIN_DEBUG("windows", "  {:#x} owner={:#x} {}x{} at {},{} class='{}' title='{}'",
                   window.handle, window.owner, window.width, window.height, window.x,
                   window.y, window.class_name, window.title);
    }
    send_window_list(*changed);
}

std::pair<std::int32_t, std::int32_t> GuestSession::picture_offset(
    std::uint64_t window_handle) const {
    const auto area = drawn_.find(window_handle);
    if (area == drawn_.end()) return {0, 0};
    return {static_cast<std::int32_t>(area->second.offset_x),
            static_cast<std::int32_t>(area->second.offset_y)};
}

void GuestSession::send_window_list(const std::vector<GuestWindow>& windows) {
    MessageWriter out{MessageId::WindowList};
    out.put_u8(watcher_->application_alive() ? 1 : 0);
    // What the guest has room for, so a mirror is not dragged past a size
    // the window could never be given; and whether it shades any window.
    GuestDesktop{room_.width(), room_.height(), shadows_.enabled()}.write(out);
    out.put_u32(static_cast<std::uint32_t>(windows.size()));
    for (const GuestWindow& window : windows) {
        // Reported as what it draws, not as the rectangle it owns.
        std::int32_t x = window.x;
        std::int32_t y = window.y;
        std::uint32_t width = window.width;
        std::uint32_t height = window.height;
        std::int32_t drawn_x = 0;
        std::int32_t drawn_y = 0;
        if (const auto area = drawn_.find(window.handle); area != drawn_.end()) {
            drawn_x = static_cast<std::int32_t>(area->second.offset_x);
            drawn_y = static_cast<std::int32_t>(area->second.offset_y);
            x += drawn_x;
            y += drawn_y;
            width = area->second.width;
            height = area->second.height;
        }

        out.put_u64(window.handle);
        out.put_u64(window.owner);
        out.put_u32(window.process_id);
        out.put_i32(x);
        out.put_i32(y);
        out.put_u32(width);
        out.put_u32(height);
        out.put_u32(window.style);
        out.put_u32(window.extended_style);
        out.put_u32(window.class_style);
        out.put_string(window.title);
        out.put_string(window.class_name);
        out.put_u8(static_cast<std::uint8_t>((window.minimized ? 1 : 0)
                                             | (window.maximized ? 2 : 0)
                                             | (window.dwm_frame ? 4 : 0)
                                             | (window.foreground ? 8 : 0)));
        out.put_u32(window.layered_flags);
        out.put_u8(window.layered_alpha);
        out.put_u32(window.color_key);
        out.put_u32(window.band);
        // The region is in the picture's coordinates; the host's window
        // begins where the drawn part of it does.
        out.put_u32(static_cast<std::uint32_t>(window.region.size()));
        for (const WindowRect& rect : window.region) {
            out.put_i32(rect.left - drawn_x);
            out.put_i32(rect.top - drawn_y);
            out.put_i32(rect.right - drawn_x);
            out.put_i32(rect.bottom - drawn_y);
        }
    }
    running_ = link_.send(out.take());
}

void GuestSession::take_route(bool shared_surfaces) {
    if (!shared_surfaces) {
        if (!shared_route_) return;
        RWIN_WARN("shared", "the host takes frames as pixels from here on");
        stop_sharing();
        recapture();
        return;
    }
    if (surface_ids_ == nullptr) surface_ids_ = std::make_unique<SurfaceIdReader>(graphics_.device());
    if (!surface_ids_->usable()) {
        RWIN_WARN("shared", "the host takes shared surfaces, but no surface id can be read here; frames go as pixels");
        return;
    }
    RWIN_INFO("shared", "the host takes shared surfaces: frames stay on the GPU");
    shared_route_ = true;
}

// The monitor's tiles are weighed against the last picture sent as pixels,
// which is not what the host shows after frames that stayed on the GPU.
void GuestSession::stop_sharing() {
    shared_route_ = false;
    rings_.clear();
    monitor_tiles_ = ChangedTiles{};
}

void GuestSession::recapture() {
    for (auto& [handle, capture] : captures_) capture = std::make_unique<WindowCapture>(graphics_, frames_);
    if (monitor_ != nullptr) {
        monitor_ = std::make_unique<MonitorCapture>(graphics_, frames_);
        monitor_tiles_ = ChangedTiles{};
    }
}

SurfaceRing& GuestSession::ring_for(std::uint64_t window) {
    auto [ring, added] = rings_.try_emplace(window);
    if (added) ring->second = std::make_unique<SurfaceRing>(graphics_, *surface_ids_, window, rings_made_);
    return *ring->second;
}

void GuestSession::note_drawn(std::uint64_t window_handle, const DrawnArea& area) {
    auto [known, added] = drawn_.try_emplace(window_handle, area);
    if (!added && known->second == area) return;
    known->second = area;
    // The reported geometry is derived from this, so it has to be said
    // again now that it has changed.
    if (watcher_ != nullptr) watcher_->forget();
}

void GuestSession::send_frames() {
    send_monitor_frame();
    std::vector<std::uint64_t> vanished;
    for (auto& [handle, capture] : captures_) {
        const WindowSurface surface{handle};
        if (surface.minimized()) continue;
        // Nothing new is the ordinary answer: the compositor produces a
        // frame only when it composed one, so a window sitting still
        // simply has no news.
        const CaptureResult grabbed = shared_route_ ? send_shared_frame(handle, *capture, surface)
                                                    : send_pixel_frame(handle, *capture, surface);
        if (!running_) return;
        if (grabbed == CaptureResult::Gone) {
            RWIN_INFO("frames", "window {:#x} can no longer be captured; dropping", handle);
            vanished.push_back(handle);
        }
    }
    for (const std::uint64_t handle : vanished) {
        captures_.erase(handle);
        drawn_.erase(handle);
        rings_.erase(handle);
        send_window_gone(handle);
    }
}

CaptureResult GuestSession::send_pixel_frame(std::uint64_t handle, WindowCapture& capture,
                                             const WindowSurface& surface) {
    const CaptureResult grabbed = capture.grab(surface);
    if (grabbed != CaptureResult::Fresh) return grabbed;
    // A window that went back to pixels has no use for its ring.
    rings_.erase(handle);
    send_pixels(handle, capture);
    return grabbed;
}

// A frame is taken only with a slot to put it in: one taken and dropped is
// a picture lost, and a window sitting still sends no other.
CaptureResult GuestSession::send_shared_frame(std::uint64_t handle, WindowCapture& capture,
                                              const WindowSurface& surface) {
    SurfaceRing& ring = ring_for(handle);
    if (!ring.has_free_slot()) {
        timing_.note("no_slot", handle);
        return CaptureResult::Unchanged;
    }
    std::optional<CaptureStream::Frame> frame;
    const CaptureResult grabbed = capture.grab(surface, frame);
    if (grabbed != CaptureResult::Fresh) return grabbed;
    // A margin round what the window draws is trimmed away, which takes its
    // pixels: such a frame goes as pixels, and its ring waits for the next.
    if (surface.margin_is_reserved() && !edges_.all_drawn(*frame)) {
        rings_.erase(handle);
        if (capture.keep(*frame, surface)) send_pixels(handle, capture);
        return grabbed;
    }
    const CaptureStream::Extent content = frame->content();
    const auto source_time = frame->source_time();
    timing_.note("copy_begin", handle, source_time);
    if (const std::optional<SurfaceRing::Delivery> delivery = ring.copy(*frame)) {
        timing_.note("copy_end", handle, source_time);
        note_drawn(handle, DrawnArea{0, 0, content.width, content.height});
        send_delivery(*delivery);
        timing_.note("send_end", handle, source_time);
        return grabbed;
    }
    RWIN_WARN("shared", "frames cannot stay on the GPU here; they go as pixels from here on");
    stop_sharing();
    if (capture.keep(*frame, surface)) send_pixels(handle, capture);
    return grabbed;
}

void GuestSession::send_pixels(std::uint64_t handle, const WindowCapture& capture) {
    note_drawn(handle, DrawnArea{capture.offset_x(), capture.offset_y(), capture.width(), capture.height()});
    MessageWriter out{MessageId::Frame};
    out.put_u64(handle);
    out.put_u32(capture.width());
    out.put_u32(capture.height());
    out.put_bytes(capture.pixels(), capture.size_bytes());
    running_ = link_.send(out.take());
}

void GuestSession::send_delivery(const SurfaceRing::Delivery& delivery) {
    if (delivery.made.has_value()) {
        MessageWriter made{MessageId::SlotRing};
        delivery.made->write(made);
        if (!link_.send(made.take())) {
            running_ = false;
            return;
        }
    }
    MessageWriter out{MessageId::FrameInSlot};
    delivery.frame.write(out);
    running_ = link_.send(out.take());
}

void GuestSession::send_monitor_frame() {
    if (monitor_ == nullptr || !running_) return;
    if (shared_route_) {
        send_monitor_shared();
        return;
    }
    const CaptureResult grabbed = monitor_->grab();
    if (grabbed == CaptureResult::Unchanged) return;
    if (grabbed == CaptureResult::Gone) {
        lose_monitor();
        return;
    }
    send_monitor_pixels();
}

void GuestSession::send_monitor_shared() {
    SurfaceRing& ring = ring_for(kMonitorWindow);
    if (!ring.has_free_slot()) return;
    std::optional<CaptureStream::Frame> frame;
    const CaptureResult grabbed = monitor_->grab(frame);
    if (grabbed == CaptureResult::Unchanged) return;
    if (grabbed == CaptureResult::Gone) {
        lose_monitor();
        return;
    }
    if (const std::optional<SurfaceRing::Delivery> delivery = ring.copy(*frame)) {
        send_delivery(*delivery);
        return;
    }
    RWIN_WARN("shared", "the monitor's frames cannot stay on the GPU here; they go as pixels from here on");
    stop_sharing();
    if (monitor_->keep(*frame)) send_monitor_pixels();
}

void GuestSession::lose_monitor() {
    RWIN_ERROR("frames", "the monitor can no longer be captured");
    monitor_.reset();
    rings_.erase(kMonitorWindow);
    send_window_gone(kMonitorWindow);
}

void GuestSession::send_monitor_pixels() {
    if (monitor_tiles_.weigh(monitor_->pixels(), monitor_->width(), monitor_->height())) {
        send_monitor_patch();
        return;
    }
    MessageWriter out{MessageId::Frame};
    out.put_u64(kMonitorWindow);
    out.put_u32(monitor_->width());
    out.put_u32(monitor_->height());
    out.put_bytes(monitor_->pixels(), monitor_->size_bytes());
    running_ = link_.send(out.take());
}

void GuestSession::send_monitor_patch() {
    const std::vector<PictureRect>& changed = monitor_tiles_.changed();
    if (changed.empty()) return;
    MessageWriter out{MessageId::FramePatch};
    out.put_u64(kMonitorWindow);
    out.put_u32(monitor_->width());
    out.put_u32(monitor_->height());
    out.put_u32(static_cast<std::uint32_t>(changed.size()));
    const std::size_t stride = static_cast<std::size_t>(monitor_->width()) * 4;
    for (const PictureRect& rect : changed) {
        rect.write(out);
        for (std::uint32_t y = rect.y; y < rect.y + rect.height; ++y) {
            out.put_bytes(monitor_->pixels() + y * stride + static_cast<std::size_t>(rect.x) * 4,
                          static_cast<std::size_t>(rect.width) * 4);
        }
    }
    running_ = link_.send(out.take());
}

void GuestSession::send_window_gone(std::uint64_t window_handle) {
    MessageWriter out{MessageId::WindowGone};
    out.put_u64(window_handle);
    running_ = link_.send(out.take());
}

}  // namespace rwin::guest
