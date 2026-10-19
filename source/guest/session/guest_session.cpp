#include "guest/session/guest_session.hpp"

#include "guest/capture/capture_clock.hpp"
#include "guest/input/key_input.hpp"
#include "guest/input/pointer_input.hpp"
#include "guest/session/multimedia_thread.hpp"
#include "guest/session/tcp_client.hpp"
#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"
#include "proto/activate.hpp"
#include "proto/clock_echo.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/frame_route.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/hello.hpp"
#include "proto/key_stroke.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/picture_rect.hpp"
#include "proto/pointer_at.hpp"
#include "proto/pointer_button.hpp"
#include "proto/pointer_delta.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/pointer_move.hpp"
#include "proto/pointer_wheel.hpp"
#include "proto/slot_free.hpp"
#include "proto/slot_ring.hpp"
#include "proto/surface_lost.hpp"
#include "proto/sys_command.hpp"
#include "proto/sys_menu.hpp"
#include "proto/sys_param.hpp"
#include "proto/window_move.hpp"

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <array>
#include <thread>

namespace rwin::guest {

namespace {

// "Show window contents while dragging", on for this logon. Off, Windows
// moves a dragged window only when the button comes up, drawing an outline
// meanwhile on a desktop nobody sees: a mirror following the window sits
// still for the whole drag and jumps at the end. Not written to the
// profile: the user's own choice comes back at the next logon.
void show_contents_while_dragging() {
    BOOL full = FALSE;
    if (::SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &full, 0) == FALSE || full != FALSE) return;
    if (::SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS, TRUE, nullptr, SPIF_SENDCHANGE) == FALSE) {
        RWIN_WARN("drag", "cannot show window contents while dragging (error {}): a window Windows moves is "
                  "mirrored where it ends", ::GetLastError());
        return;
    }
    RWIN_INFO("drag", "showing window contents while dragging, for this logon");
}

// A cursor's id is its handle and its picture together: Windows hands a
// destroyed cursor's handle to the next one made, and a host that kept
// pictures by handle alone showed the old picture for the new cursor.
std::uint64_t cursor_id_of(std::uint64_t handle, const CursorPicture& picture) {
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t hash = kOffset;
    const auto mix = [&hash](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFF;
            hash *= kPrime;
        }
    };
    mix(handle);
    for (const CursorPicture::Frame& frame : picture.frames) {
        mix((static_cast<std::uint64_t>(frame.width) << 32) | frame.height);
        mix((static_cast<std::uint64_t>(frame.hotspot_x) << 32) | frame.hotspot_y);
        for (const std::uint8_t byte : frame.bgra) {
            hash ^= byte;
            hash *= kPrime;
        }
    }
    // Never 0, which a hidden cursor is.
    return hash == 0 ? 1 : hash;
}

bool is_input(MessageId id) {
    return id == MessageId::PointerMove || id == MessageId::PointerButton || id == MessageId::PointerDelta
           || id == MessageId::PointerWheel || id == MessageId::Key || id == MessageId::PointerAt
           || id == MessageId::PointerLeave;
}

}  // namespace

GuestSession::GuestSession(TcpClient& link, std::uint32_t frames_per_second)
    : link_(link), outbox_(link), frame_interval_ms_(1000 / (frames_per_second != 0 ? frames_per_second : 1)) {}

void GuestSession::run() {
    RWIN_INFO("session", "serving; frame interval {} ms, {}", frame_interval_ms_, Hello::ours().build);
    MessageWriter hello{MessageId::GuestHello};
    Hello::ours().write(hello);
    send(hello.take());
    const MultimediaThread timely;
    std::thread reader{[this] { read_link(); }};
    while (running_ && !outbox_.failed()) {
        wait_for_work();
        serve_queued();
        if (!running_) break;
        send_frames();
        report_cursor();
        const std::lock_guard held{shared_};
        report_grab();
    }
    running_ = false;
    reader.join();
    windows_.reset();
}

// The reader's: every message decoded here, input put into the guest at
// once and the rest handed to the main loop.
void GuestSession::read_link() {
    const MultimediaThread timely;
    std::vector<std::uint8_t> incoming;
    while (running_) {
        if (!link_.wait_readable(kReadWaitMs)) continue;
        if (!link_.receive(incoming)) break;
        decoder_.feed(incoming.data(), incoming.size());
        incoming.clear();
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        while (running_ && decoder_.next(&payload, &size)) {
            MessageReader message{payload, size};
            if (!greeted_) {
                greet(message);
            } else if (!handle_input(message)) {
                queue(payload, size);
            }
        }
        if (decoder_.overflowed()) break;
    }
    running_ = false;
    woken_.raise();
}

void GuestSession::greet(MessageReader& message) {
    const std::optional<Hello> hello =
        message.id() == MessageId::HostHello ? Hello::read(message) : std::optional<Hello>{};
    if (!hello.has_value()) {
        RWIN_ERROR("link", "the host's first message is {}, not a wire 6 hello: it is a remotewin from another "
                   "build. Put the halves built together on both sides", static_cast<int>(message.id()));
        running_ = false;
        return;
    }
    features_ = FeatureSet{Hello::ours().features, hello->features};
    if (const std::string refusal = features_.refusal(); !refusal.empty()) {
        RWIN_ERROR("link", "{} ({})", refusal, hello->build);
        running_ = false;
        return;
    }
    RWIN_INFO("link", "the host is {}: {}", hello->build, features_.describe());
    windows_ = std::make_unique<WindowWatch>(directory_, outbox_, features_);
    greeted_ = true;
}

void GuestSession::queue(const std::uint8_t* payload, std::size_t size) {
    {
        const std::lock_guard held{inbox_lock_};
        inbox_.emplace_back(payload, payload + size);
    }
    woken_.raise();
}

void GuestSession::wait_for_work() {
    const HANDLE waited[] = {static_cast<HANDLE>(woken_.event()), static_cast<HANDLE>(frames_.event())};
    ::WaitForMultipleObjects(2, waited, FALSE, frame_interval_ms_);
}

void GuestSession::serve_queued() {
    {
        const std::lock_guard held{inbox_lock_};
        serving_.swap(inbox_);
    }
    for (const std::vector<std::uint8_t>& payload : serving_) {
        if (!running_) break;
        MessageReader message{payload.data(), payload.size()};
        handle(message);
    }
    serving_.clear();
}

void GuestSession::send(const std::vector<std::uint8_t>& bytes) {
    outbox_.send(bytes);
    if (outbox_.failed()) running_ = false;
}

void GuestSession::handle(MessageReader& message) {
    switch (message.id()) {
        case MessageId::Subscribe:
            if (const std::optional<WindowId> window = WindowId::read(message)) subscribe(*window);
            return;
        case MessageId::Unsubscribe:
            if (const std::optional<WindowId> window = WindowId::read(message)) unsubscribe(*window);
            return;
        case MessageId::WatchMonitor:
            // Said once per connection; a second is the same request.
            if (monitor_ != nullptr) return;
            RWIN_INFO("scope", "asked to mirror the whole monitor");
            monitor_ = std::make_unique<MonitorCapture>(graphics_, frames_);
            monitor_tiles_ = ChangedTiles{};
            return;
        case MessageId::PopupDismiss:
            if (const std::optional<WindowId> popup = WindowId::read(message)) windows_->dismiss(*popup);
            return;
        case MessageId::SysCommand:
            if (const std::optional<SysCommand> command = SysCommand::read(message)) windows_->ask(*command);
            return;
        case MessageId::SysMenu:
            if (const std::optional<SysMenu> menu = SysMenu::read(message)) windows_->ask(*menu);
            return;
        case MessageId::WindowMove:
            if (const std::optional<WindowMove> move = WindowMove::read(message)) windows_->ask(*move);
            return;
        case MessageId::HostSysParam: {
            const std::optional<SysParam> setting = SysParam::read(message);
            if (setting.has_value() && setting->param == SysParam::Param::DragFullWindows && setting->enabled) {
                show_contents_while_dragging();
            }
            return;
        }
        default:
            handle_frames(message);
            return;
    }
}

void GuestSession::handle_frames(MessageReader& message) {
    switch (message.id()) {
        case MessageId::FrameRoute:
            if (const std::optional<FrameRoute> route = FrameRoute::read(message)) {
                take_route(route->shared_surfaces);
            } else {
                RWIN_WARN("shared", "malformed frame route");
            }
            return;
        case MessageId::SlotFree: {
            const std::optional<SlotFree> freed = SlotFree::read(message);
            if (!freed.has_value()) {
                RWIN_WARN("shared", "malformed slot free");
                return;
            }
            if (const auto ring = rings_.find(freed->window.key()); ring != rings_.end()) ring->second->free(*freed);
            return;
        }
        case MessageId::WindowPixels:
            if (const std::optional<WindowId> window = WindowId::read(message)) {
                RWIN_WARN("shared", "{}: the host draws this window's frames from pixels from here on", *window);
                pixels_only_.insert(window->key());
                rings_.erase(window->key());
            }
            return;
        default:
            RWIN_WARN("session", "unknown message id {}", static_cast<int>(message.id()));
            return;
    }
}

void GuestSession::subscribe(WindowId window) {
    if (directory_.handle_of(window) == nullptr) {
        RWIN_DEBUG("session", "subscription to {}, which is not a window here", window);
        return;
    }
    subscriptions_.try_emplace(window.key(), Subscription{std::make_unique<WindowCapture>(graphics_, frames_)});
    {
        const std::lock_guard held{shared_};
        subscribed_.insert(window.key());
    }
    RWIN_INFO("session", "subscribed to window {} ({} now)", window, subscriptions_.size());
}

void GuestSession::unsubscribe(WindowId window) {
    subscriptions_.erase(window.key());
    {
        const std::lock_guard held{shared_};
        subscribed_.erase(window.key());
    }
    rings_.erase(window.key());
    edges_.erase(window.key());
    pixels_only_.erase(window.key());
    if (directory_.set_picture(window, std::nullopt) && windows_ != nullptr) windows_->reread(window);
    RWIN_INFO("session", "unsubscribed from window {}", window);
}

// Put into the guest from the thread that read it, whatever the main loop
// is doing. What it shares with the main loop is under shared_.
bool GuestSession::handle_input(MessageReader& message) {
    const MessageId id = message.id();
    if (id == MessageId::Activate) {
        if (const std::optional<Activate> activation = Activate::read(message)) windows_->ask(*activation);
        return true;
    }
    if (id == MessageId::ClockProbe) {
        const std::uint64_t probed = message.u64();
        if (!message.ok()) return true;
        MessageWriter out{MessageId::ClockEcho};
        ClockEcho{probed, capture_clock_now()}.write(out);
        send(out.take());
        return true;
    }
    if (!is_input(id)) return false;
    const std::lock_guard held{shared_};
    switch (id) {
        case MessageId::PointerMove:
            if (const std::optional<PointerMove> move = PointerMove::read(message)) move_pointer(*move);
            break;
        case MessageId::PointerButton:
            if (const std::optional<PointerButton> press = PointerButton::read(message)) press_button(*press);
            break;
        case MessageId::PointerDelta:
            if (const std::optional<PointerDelta> delta = PointerDelta::read(message)) {
                report_grab();
                grabs_.nudging();
                nudge_pointer(delta->dx, delta->dy);
            }
            break;
        case MessageId::PointerAt:
            if (const std::optional<PointerAt> at = PointerAt::read(message)) {
                report_grab();
                move_pointer_on_screen(at->x, at->y);
                grabs_.put_at(at->x, at->y);
                if (at->button != PointerAt::kNoButton) {
                    target_.pressed(at->pressed);
                    press_pointer_on_screen(at->x, at->y, static_cast<PointerButton::Button>(at->button), at->pressed);
                }
            }
            break;
        case MessageId::PointerWheel:
            if (const std::optional<PointerWheel> wheel = PointerWheel::read(message)) turn_wheel_over(*wheel);
            break;
        case MessageId::Key:
            if (const std::optional<KeyStroke> stroke = KeyStroke::read(message)) put_key(*stroke);
            break;
        case MessageId::PointerLeave:
            if (const std::optional<WindowId> window = WindowId::read(message)) leave(*window);
            break;
        default:
            break;
    }
    return true;
}

std::optional<WindowSurface> GuestSession::surface_of(WindowId window, std::int32_t& into_x,
                                                      std::int32_t& into_y) const {
    void* handle = directory_.handle_of(window);
    if (handle == nullptr) return std::nullopt;
    const std::optional<Rect> picture = directory_.picture_of(window);
    into_x = picture.has_value() ? picture->x : 0;
    into_y = picture.has_value() ? picture->y : 0;
    return WindowSurface{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle))};
}

void GuestSession::move_pointer(const PointerMove& move) {
    if (move.window == kMonitorWindow) {
        if (monitor_at_.has_value()) move_pointer_on_screen(monitor_at_->x + move.x, monitor_at_->y + move.y);
        return;
    }
    std::int32_t into_x = 0;
    std::int32_t into_y = 0;
    const std::optional<WindowSurface> surface = surface_of(move.window, into_x, into_y);
    if (!surface.has_value()) return;
    // What became of the last move is weighed before this one is made: the
    // evidence of mouse look (PointerGrabRule).
    report_grab();
    target_.raise(surface->handle(), surface->screen_x(move.x + into_x), surface->screen_y(move.y + into_y), false);
    if (const auto at = move_pointer_to(*surface, move.x + into_x, move.y + into_y)) {
        grabs_.put_at(at->first, at->second);
    }
}

void GuestSession::press_button(const PointerButton& press) {
    if (press.window == kMonitorWindow) {
        if (monitor_at_.has_value()) {
            press_pointer_on_screen(monitor_at_->x + press.x, monitor_at_->y + press.y, press.button, press.pressed);
        }
        return;
    }
    if (grabs_.current().kind == PointerGrab::Kind::Lock) {
        press_pointer_in_place(press.button, press.pressed);
        return;
    }
    std::int32_t into_x = 0;
    std::int32_t into_y = 0;
    const std::optional<WindowSurface> surface = surface_of(press.window, into_x, into_y);
    if (!surface.has_value()) return;
    if (press.pressed) {
        target_.raise(surface->handle(), surface->screen_x(press.x + into_x), surface->screen_y(press.y + into_y), true);
    }
    target_.pressed(press.pressed);
    rwin::guest::press_pointer(*surface, press.x + into_x, press.y + into_y, press.button, press.pressed);
}

void GuestSession::turn_wheel_over(const PointerWheel& wheel) {
    if (wheel.window == kMonitorWindow) {
        if (monitor_at_.has_value()) {
            turn_wheel_on_screen(monitor_at_->x + wheel.x, monitor_at_->y + wheel.y, wheel.dx, wheel.dy);
        }
        return;
    }
    if (grabs_.current().kind == PointerGrab::Kind::Lock) {
        turn_wheel_in_place(wheel.dx, wheel.dy);
        return;
    }
    std::int32_t into_x = 0;
    std::int32_t into_y = 0;
    const std::optional<WindowSurface> surface = surface_of(wheel.window, into_x, into_y);
    if (!surface.has_value()) return;
    target_.raise(surface->handle(), surface->screen_x(wheel.x + into_x), surface->screen_y(wheel.y + into_y), true);
    turn_wheel(*surface, wheel.x + into_x, wheel.y + into_y, wheel.dx, wheel.dy);
}

// The pointer left the mirror, and the guest's cursor is still over the
// window: hover highlights and tooltips stay up until it is not. It goes to
// a corner of the work area that no window the host is told about covers -
// the desktop's, which nobody sees - and the window gets its mouse leave.
void GuestSession::leave(WindowId window) {
    if (grabs_.current().kind != PointerGrab::Kind::None) return;
    std::int32_t into_x = 0;
    std::int32_t into_y = 0;
    const std::optional<WindowSurface> surface = surface_of(window, into_x, into_y);
    POINT cursor{};
    if (!surface.has_value() || !surface->valid() || ::GetCursorPos(&cursor) == FALSE) return;
    const bool inside = cursor.x >= surface->screen_x(0) && cursor.y >= surface->screen_y(0)
                        && cursor.x < surface->screen_x(static_cast<std::int32_t>(surface->width()))
                        && cursor.y < surface->screen_y(static_cast<std::int32_t>(surface->height()));
    if (!inside) return;
    RECT area{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0) == FALSE) return;
    const std::array<POINT, 4> corners{POINT{area.left, area.top}, POINT{area.right - 1, area.top},
                                       POINT{area.left, area.bottom - 1}, POINT{area.right - 1, area.bottom - 1}};
    for (const POINT corner : corners) {
        HWND under = ::GetAncestor(::WindowFromPoint(corner), GA_ROOT);
        if (under != nullptr && directory_.id_of(under).has_value()) continue;
        move_pointer_on_screen(corner.x, corner.y);
        RWIN_TRACE("input", "pointer left {}: cursor to {},{}", window, corner.x, corner.y);
        return;
    }
}

void GuestSession::report_cursor() {
    const std::optional<GuestCursor> cursor = cursor_.changed();
    if (!cursor.has_value()) return;
    GuestCursor shown = *cursor;
    if (cursor->shown) {
        std::optional<CursorPicture> picture =
            cursor_pictures_.decode(reinterpret_cast<void*>(static_cast<std::uintptr_t>(cursor->id)));
        if (!picture.has_value()) {
            RWIN_WARN("cursor", "cursor {:#x} could not be decoded", cursor->id);
            return;
        }
        shown.id = cursor_id_of(cursor->id, *picture);
        if (!sent_cursors_.shown(shown.id)) {
            picture->id = shown.id;
            const CursorPicture::Frame& first = picture->frames.front();
            RWIN_DEBUG("cursor", "new cursor {:#x} (handle {:#x}) {}: {} picture(s) {}x{} hotspot {},{}", shown.id,
                       cursor->id, name_of(picture->system), picture->frames.size(), first.width, first.height,
                       first.hotspot_x, first.hotspot_y);
            MessageWriter shape{MessageId::CursorShape};
            picture->write(shape);
            send(shape.take());
        }
    }
    MessageWriter changed{MessageId::CursorChanged};
    shown.write(changed);
    send(changed.take());
}

WindowId GuestSession::mirrored_foreground() const {
    const std::optional<WindowId> foreground = directory_.id_of(::GetForegroundWindow());
    return foreground.has_value() && subscribed_.contains(foreground->key()) ? *foreground : WindowId{};
}

void GuestSession::report_grab() {
    const std::optional<PointerGrab> grab = grabs_.changed(mirrored_foreground());
    if (!grab.has_value()) return;
    MessageWriter out{MessageId::PointerGrab};
    grab->write(out);
    send(out.take());
}

void GuestSession::take_route(bool shared_surfaces) {
    if (!shared_surfaces) {
        if (!shared_route_) return;
        RWIN_WARN("shared", "the host takes frames as pixels from here on");
        stop_sharing();
        recapture();
        return;
    }
    if (shared_route_) {
        RWIN_WARN("shared", "the host lost every shared surface (its GPU device was reset): every ring made afresh");
        rings_.clear();
        pixels_only_.clear();
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
    for (auto& [key, subscription] : subscriptions_) {
        subscription.capture = std::make_unique<WindowCapture>(graphics_, frames_);
        subscription.lost = false;
    }
    if (monitor_ != nullptr) {
        monitor_ = std::make_unique<MonitorCapture>(graphics_, frames_);
        monitor_tiles_ = ChangedTiles{};
    }
}

PictureEdges& GuestSession::edges_for(WindowId window) {
    auto [edges, added] = edges_.try_emplace(window.key());
    if (added) edges->second = std::make_unique<PictureEdges>(graphics_);
    return *edges->second;
}

SurfaceRing& GuestSession::ring_for(WindowId window) {
    auto [ring, added] = rings_.try_emplace(window.key());
    if (added) ring->second = std::make_unique<SurfaceRing>(graphics_, *surface_ids_, window, rings_made_);
    return *ring->second;
}

// Where the frames lie in the window's frame, when the capture trimmed a
// margin away: the GEOMETRY of the window says so, and input is measured
// from there.
void GuestSession::note_drawn(WindowId id, const WindowCapture& capture, const WindowSurface& surface) {
    const bool whole = capture.offset_x() == 0 && capture.offset_y() == 0 && capture.width() == surface.width()
                       && capture.height() == surface.height();
    const std::optional<Rect> trimmed =
        whole ? std::nullopt
              : std::optional<Rect>(Rect{static_cast<std::int32_t>(capture.offset_x()),
                                         static_cast<std::int32_t>(capture.offset_y()), capture.width(),
                                         capture.height()});
    if (directory_.set_picture(id, trimmed) && windows_ != nullptr) windows_->reread(id);
}

void GuestSession::send_frames() {
    send_monitor_frame();
    std::vector<std::uint64_t> gone;
    for (auto& [key, subscription] : subscriptions_) {
        const WindowId id = WindowId::of_key(key);
        if (directory_.handle_of(id) == nullptr) {
            gone.push_back(key);
            continue;
        }
        send_window_frame(id, subscription);
        if (!running_) return;
    }
    // WindowGone said it already; the host's Unsubscribe may never come.
    for (const std::uint64_t key : gone) {
        subscriptions_.erase(key);
        rings_.erase(key);
        edges_.erase(key);
        pixels_only_.erase(key);
        const std::lock_guard held{shared_};
        subscribed_.erase(key);
    }
}

// A capture that fails is not the window gone - that is WindowGone's to say
// - and the window is captured afresh a while later, without being asked.
void GuestSession::send_window_frame(WindowId id, Subscription& subscription) {
    const WindowSurface surface{static_cast<std::uint64_t>(id.hwnd)};
    if (surface.minimized()) return;
    const Clock::time_point now = Clock::now();
    if (subscription.lost) {
        if (now < subscription.retry_at) return;
        subscription.capture = std::make_unique<WindowCapture>(graphics_, frames_);
    }
    const bool shared = shared_route_ && !pixels_only_.contains(id.key());
    const CaptureResult grabbed = shared ? send_shared_frame(id, *subscription.capture, surface)
                                         : send_pixel_frame(id, *subscription.capture, surface);
    if (grabbed != CaptureResult::Gone) {
        if (subscription.lost) RWIN_INFO("frames", "{} is captured again", id);
        subscription.lost = false;
        return;
    }
    subscription.retry_at = now + kRecaptureAfter;
    if (subscription.lost) return;
    subscription.lost = true;
    rings_.erase(id.key());
    RWIN_INFO("frames", "{} cannot be captured now; its frames stop until it can", id);
    MessageWriter out{MessageId::SurfaceLost};
    SurfaceLost{id, SurfaceLost::Reason::CaptureFailed}.write(out);
    send(out.take());
}

CaptureResult GuestSession::send_pixel_frame(WindowId id, WindowCapture& capture, const WindowSurface& surface) {
    const CaptureResult grabbed = capture.grab(surface);
    if (grabbed != CaptureResult::Fresh) return grabbed;
    // A window that went back to pixels has no use for its ring.
    rings_.erase(id.key());
    note_drawn(id, capture, surface);
    send_pixels(id, capture);
    return grabbed;
}

// A frame is taken only with a slot to put it in: one taken and dropped is
// a picture lost, and a window sitting still sends no other.
CaptureResult GuestSession::send_shared_frame(WindowId id, WindowCapture& capture, const WindowSurface& surface) {
    SurfaceRing& ring = ring_for(id);
    if (!ring.has_free_slot()) {
        timing_.note("no_slot", id.key());
        return CaptureResult::Unchanged;
    }
    std::optional<CaptureStream::Frame> frame;
    const CaptureResult grabbed = capture.grab(surface, frame);
    if (grabbed != CaptureResult::Fresh) return grabbed;
    // A margin round what the window draws is trimmed away, which takes its
    // pixels: such a frame goes as pixels, and its ring waits for the next.
    if (surface.margin_is_reserved() && !edges_for(id).all_drawn(*frame)) {
        rings_.erase(id.key());
        if (capture.keep(*frame, surface)) {
            note_drawn(id, capture, surface);
            send_pixels(id, capture);
        }
        return grabbed;
    }
    const auto source_time = frame->source_time();
    timing_.note("copy_begin", id.key(), source_time);
    if (const std::optional<SurfaceRing::Delivery> delivery = ring.copy(*frame)) {
        timing_.note("copy_end", id.key(), source_time);
        if (directory_.set_picture(id, std::nullopt) && windows_ != nullptr) windows_->reread(id);
        send_delivery(*delivery);
        timing_.note("send_end", id.key(), source_time);
        return grabbed;
    }
    RWIN_WARN("shared", "frames cannot stay on the GPU here; they go as pixels from here on");
    stop_sharing();
    if (capture.keep(*frame, surface)) {
        note_drawn(id, capture, surface);
        send_pixels(id, capture);
    }
    return grabbed;
}

void GuestSession::send_pixels(WindowId id, const WindowCapture& capture) {
    MessageWriter out{MessageId::Frame};
    id.write(out);
    out.put_u32(capture.width());
    out.put_u32(capture.height());
    out.put_bytes(capture.pixels(), capture.size_bytes());
    send(out.take());
}

void GuestSession::send_delivery(const SurfaceRing::Delivery& delivery) {
    if (delivery.made.has_value()) {
        MessageWriter made{MessageId::SlotRing};
        delivery.made->write(made);
        send(made.take());
    }
    MessageWriter out{MessageId::FrameInSlot};
    delivery.frame.write(out);
    send(out.take());
}

void GuestSession::send_monitor_frame() {
    if (monitor_ == nullptr || !running_) return;
    if (shared_route_ && !pixels_only_.contains(kMonitorWindow.key())) {
        send_monitor_shared();
        return;
    }
    const CaptureResult grabbed = monitor_->grab();
    publish_monitor_origin();
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
    publish_monitor_origin();
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

void GuestSession::publish_monitor_origin() {
    const std::lock_guard held{shared_};
    monitor_at_ = ScreenPoint{monitor_->screen_x(0), monitor_->screen_y(0)};
}

void GuestSession::lose_monitor() {
    RWIN_ERROR("frames", "the monitor can no longer be captured");
    monitor_.reset();
    {
        const std::lock_guard held{shared_};
        monitor_at_.reset();
    }
    rings_.erase(kMonitorWindow.key());
    MessageWriter out{MessageId::SurfaceLost};
    SurfaceLost{kMonitorWindow, SurfaceLost::Reason::CaptureFailed}.write(out);
    send(out.take());
}

void GuestSession::send_monitor_pixels() {
    if (monitor_tiles_.weigh(monitor_->pixels(), monitor_->width(), monitor_->height())) {
        send_monitor_patch();
        return;
    }
    MessageWriter out{MessageId::Frame};
    kMonitorWindow.write(out);
    out.put_u32(monitor_->width());
    out.put_u32(monitor_->height());
    out.put_bytes(monitor_->pixels(), monitor_->size_bytes());
    send(out.take());
}

void GuestSession::send_monitor_patch() {
    const std::vector<PictureRect>& changed = monitor_tiles_.changed();
    if (changed.empty()) return;
    MessageWriter out{MessageId::FramePatch};
    kMonitorWindow.write(out);
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
    send(out.take());
}

}  // namespace rwin::guest
