#include "host/link/guest_link.hpp"

#include "host/latency/frame_stamps.hpp"
#include "logging/logger.hpp"
#include "proto/activate.hpp"
#include "proto/feature.hpp"
#include "proto/frame_route.hpp"
#include "proto/hello.hpp"
#include "proto/key_stroke.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/min_max.hpp"
#include "proto/picture_rect.hpp"
#include "proto/pointer_at.hpp"
#include "proto/pointer_button.hpp"
#include "proto/pointer_delta.hpp"
#include "proto/pointer_move.hpp"
#include "proto/pointer_wheel.hpp"
#include "proto/slot_free.hpp"
#include "proto/surface_lost.hpp"
#include "proto/sync_begin.hpp"
#include "proto/sys_menu.hpp"
#include "proto/sys_param.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_move.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace rwin::host {

namespace {

// Read this much at a time; a frame of pixels takes many reads.
constexpr std::size_t kChunk = 256 * 1024;
constexpr int kBacklog = 4;

bool valid_picture_size(std::uint32_t width, std::uint32_t height) {
    constexpr auto kLargestStride = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    return width != 0 && height != 0 && width <= kLargestStride / 4 && height <= kLargestStride;
}

Box box_of(const Rect& rect) {
    return Box{rect.x, rect.y, static_cast<int>(rect.width), static_cast<int>(rect.height)};
}

}  // namespace

GuestLink::GuestLink(EventLoop& loop) : loop_(loop), chunk_(kChunk) {}

GuestLink::~GuestLink() = default;

bool GuestLink::listen(std::uint16_t port, bool loopback_only) {
    UniqueFd listener{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!listener.valid()) {
        RWIN_ERROR("link", "cannot make a socket: {}", std::strerror(errno));
        return false;
    }
    const int reuse = 1;
    static_cast<void>(::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(listener.get(), kBacklog) != 0) {
        RWIN_ERROR("link", "cannot listen on port {}: {}", port, std::strerror(errno));
        return false;
    }
    socklen_t length = sizeof(address);
    if (::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        RWIN_ERROR("link", "cannot read the port listened on: {}", std::strerror(errno));
        return false;
    }
    port_ = ntohs(address.sin_port);
    listener_ = std::move(listener);
    listening_ = loop_.watch(listener_.get(), POLLIN, [this](short) { accept_guest(); });
    RWIN_INFO("link", "listening on {}:{}", loopback_only ? "127.0.0.1" : "0.0.0.0", port_);
    return true;
}

void GuestLink::accept_guest() {
    sockaddr_in peer{};
    socklen_t length = sizeof(peer);
    UniqueFd incoming{::accept4(listener_.get(), reinterpret_cast<sockaddr*>(&peer), &length,
                                SOCK_NONBLOCK | SOCK_CLOEXEC)};
    if (!incoming.valid()) return;
    if (guest_.valid()) {
        // One guest per link. A second dial-in is a stale process that has
        // not noticed it was replaced.
        RWIN_WARN("link", "a guest is already connected; refusing the second");
        return;
    }
    const int no_delay = 1;
    static_cast<void>(::setsockopt(incoming.get(), IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)));
    guest_ = std::move(incoming);
    greeted_ = false;
    features_ = FeatureSet{};
    windows_ = GuestWindows{};
    decoder_ = FrameDecoder{};
    unsent_.clear();
    guest_watch_ = loop_.watch(guest_.get(), POLLIN, [this](short events) {
        if ((events & POLLOUT) != 0) write_pending();
        if ((events & (POLLIN | POLLHUP | POLLERR)) != 0) read_guest();
    });
    std::array<char, INET_ADDRSTRLEN> from{};
    ::inet_ntop(AF_INET, &peer.sin_addr, from.data(), from.size());
    RWIN_INFO("link", "guest dialled in from {}", from.data());
    MessageWriter hello{MessageId::HostHello};
    Hello::ours().write(hello);
    const std::vector<std::uint8_t> bytes = hello.take();
    write_now(bytes.data(), bytes.size());
}

void GuestLink::drop_guest() {
    if (!guest_.valid()) return;
    RWIN_INFO("link", "guest disconnected");
    guest_watch_.reset();
    guest_ = UniqueFd{};
    features_ = FeatureSet{};
    unsent_.clear();
    disconnected();
}

void GuestLink::read_guest() {
    while (guest_.valid()) {
        const ssize_t got = ::read(guest_.get(), chunk_.data(), chunk_.size());
        if (got == 0) {
            drop_guest();
            return;
        }
        if (got < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            RWIN_WARN("link", "reading the guest failed: {}", std::strerror(errno));
            drop_guest();
            return;
        }
        decoder_.feed(chunk_.data(), static_cast<std::size_t>(got));
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        while (guest_.valid() && decoder_.next(&payload, &size)) {
            if (greeted_) {
                dispatch(payload, size);
            } else {
                greet(payload, size);
            }
        }
        if (guest_.valid() && decoder_.overflowed()) {
            RWIN_ERROR("link", "guest framing is broken; closing");
            drop_guest();
            return;
        }
    }
}

void GuestLink::write_now(const std::uint8_t* bytes, std::size_t size) {
    if (!unsent_.empty()) {
        unsent_.insert(unsent_.end(), bytes, bytes + size);
        return;
    }
    while (size > 0) {
        const ssize_t wrote = ::send(guest_.get(), bytes, size, MSG_NOSIGNAL);
        if (wrote < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            RWIN_WARN("link", "writing to the guest failed: {}", std::strerror(errno));
            return;
        }
        bytes += wrote;
        size -= static_cast<std::size_t>(wrote);
    }
    if (size == 0) return;
    unsent_.assign(bytes, bytes + size);
    guest_watch_->want(POLLIN | POLLOUT);
}

void GuestLink::write_pending() {
    std::size_t sent = 0;
    while (sent < unsent_.size()) {
        const ssize_t wrote = ::send(guest_.get(), unsent_.data() + sent, unsent_.size() - sent, MSG_NOSIGNAL);
        if (wrote < 0) {
            if (errno == EINTR) continue;
            break;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    unsent_.erase(unsent_.begin(), unsent_.begin() + static_cast<std::ptrdiff_t>(sent));
    if (unsent_.empty() && guest_watch_ != nullptr) guest_watch_->want(POLLIN);
}

void GuestLink::greet(const std::uint8_t* payload, std::size_t size) {
    MessageReader message{payload, size};
    if (message.id() != MessageId::GuestHello) {
        refuse("the guest half's first message is " + std::to_string(static_cast<int>(message.id()))
               + ", not its hello: it is from another build than this remotewin. Put the remotewin-guest.exe "
                 "built with this remotewin in the guest");
        return;
    }
    const std::optional<Hello> hello = Hello::read(message);
    if (!hello.has_value()) {
        refuse("the guest half's hello is malformed: it is from another build than this remotewin. Put the "
               "remotewin-guest.exe built with this remotewin in the guest");
        return;
    }
    const FeatureSet features{Hello::ours().features, hello->features};
    if (const std::string refusal = features.refusal(); !refusal.empty()) {
        refuse(refusal);
        return;
    }
    features_ = features;
    greeted_ = true;
    RWIN_INFO("link", "the guest half is '{}'; speaking {}", hello->build, features_.describe());
    connected();
}

void GuestLink::refuse(const std::string& reason) {
    RWIN_ERROR("link", "{}", reason);
    guest_watch_.reset();
    guest_ = UniqueFd{};
    mismatched(reason);
}

void GuestLink::dispatch(const std::uint8_t* payload, std::size_t size) {
    MessageReader message{payload, size};
    if (dispatch_windows(message) || dispatch_frames(message) || dispatch_pointer(message)) return;
    if (message.id() == MessageId::ClockEcho) {
        if (const std::optional<ClockEcho> echo = ClockEcho::read(message)) {
            clock_echoed(*echo, now_ns());
        } else {
            RWIN_WARN("link", "malformed clock echo");
        }
        return;
    }
    if (message.id() == MessageId::WindowIcon) {
        read_icon(message);
        return;
    }
    RWIN_WARN("link", "unknown message id {}", static_cast<int>(message.id()));
}

bool GuestLink::dispatch_windows(MessageReader& message) {
    switch (message.id()) {
        case MessageId::SyncBegin:
            if (const std::optional<SyncBegin> begin = SyncBegin::read(message)) {
                RWIN_INFO("link", "the guest says every window afresh ({})",
                          begin->reason == SyncBegin::Reason::Connect ? "connect" : "resync");
                windows_.begin_sync();
                sync_began();
            } else {
                RWIN_WARN("link", "malformed sync begin");
            }
            return true;
        case MessageId::SyncEnd: read_sync_end(); return true;
        case MessageId::WindowNew: read_window_new(message); return true;
        case MessageId::WindowChanged: read_window_changed(message); return true;
        case MessageId::WindowGone: read_window_gone(message); return true;
        case MessageId::ZOrder:
            if (std::optional<ZOrder> order = ZOrder::read(message)) {
                windows_.restack(*order);
                restacked(*order);
            } else {
                RWIN_WARN("link", "malformed z-order");
            }
            return true;
        case MessageId::MinMax:
            if (const std::optional<MinMax> limits = MinMax::read(message)) {
                if (windows_.limit(*limits)) window_limited(limits->window);
            } else {
                RWIN_WARN("link", "malformed min/max");
            }
            return true;
        case MessageId::GuestSysParam: read_sys_param(message); return true;
        default: return false;
    }
}

bool GuestLink::dispatch_frames(MessageReader& message) {
    switch (message.id()) {
        case MessageId::Frame: read_frame(message); return true;
        case MessageId::FramePatch: read_patch(message); return true;
        case MessageId::SlotRing:
            if (const std::optional<SlotRing> ring = SlotRing::read(message)) {
                ring_announced(*ring);
            } else {
                RWIN_WARN("link", "malformed slot ring");
            }
            return true;
        case MessageId::FrameInSlot:
            if (const std::optional<FrameInSlot> frame = FrameInSlot::read(message)) {
                RWIN_TRACE("link", "frame {} in slot {} of ring {}, copy {}", frame->window, frame->slot,
                           frame->ring, frame->copies);
                frame_in_slot(*frame);
            } else {
                RWIN_WARN("link", "malformed frame in slot");
            }
            return true;
        case MessageId::SurfaceLost:
            if (const std::optional<SurfaceLost> lost = SurfaceLost::read(message)) {
                RWIN_INFO("link", "the guest lost {}'s frames", lost->window);
                surface_lost(lost->window);
            } else {
                RWIN_WARN("link", "malformed surface lost");
            }
            return true;
        default: return false;
    }
}

bool GuestLink::dispatch_pointer(MessageReader& message) {
    switch (message.id()) {
        case MessageId::MoveSizeStart:
            if (const std::optional<MoveSizeStart> start = MoveSizeStart::read(message)) {
                RWIN_INFO("link", "Windows began moving or sizing {} (kind {})", start->window,
                          static_cast<int>(start->kind));
                move_size_started(*start);
            } else {
                RWIN_WARN("link", "malformed move/size start");
            }
            return true;
        case MessageId::MoveSizeEnd:
            if (const std::optional<MoveSizeEnd> end = MoveSizeEnd::read(message)) {
                move_size_ended(*end);
            } else {
                RWIN_WARN("link", "malformed move/size end");
            }
            return true;
        case MessageId::CursorShape:
            if (const std::optional<CursorPicture> picture = CursorPicture::read(message)) {
                cursor_pictured(*picture);
            } else {
                RWIN_WARN("link", "malformed cursor picture");
            }
            return true;
        case MessageId::CursorChanged:
            if (const std::optional<GuestCursor> cursor = GuestCursor::read(message)) cursor_changed(*cursor);
            return true;
        case MessageId::PointerGrab:
            if (const std::optional<PointerGrab> grab = PointerGrab::read(message)) {
                pointer_grabbed(*grab);
            } else {
                RWIN_WARN("link", "malformed pointer grab");
            }
            return true;
        default: return false;
    }
}

// A WindowNew for a window known already is that window said whole again,
// as a resync says it.
void GuestLink::read_window_new(MessageReader& message) {
    const std::optional<WindowId> id = WindowId::read(message);
    WindowFields fields;
    std::uint32_t mask = 0;
    if (!id.has_value() || id->none() || !WindowFields::read(message, fields, mask)) {
        RWIN_WARN("link", "malformed new window");
        return;
    }
    const GuestWindows::Told told = windows_.tell(*id, std::move(fields));
    const GuestWindow& window = *windows_.find(*id);
    RWIN_DEBUG("link", "window {} {} '{}' class '{}' owner {} pid {} frame {},{} {}x{}", *id,
               told.known ? "said again" : "new", window.fields().title, window.fields().class_name,
               window.fields().owner, window.fields().app_id.process_id, window.fields().geometry.frame.x,
               window.fields().geometry.frame.y, window.fields().geometry.frame.width,
               window.fields().geometry.frame.height);
    if (!told.known) {
        window_new(*id);
    } else if (told.changed != 0) {
        window_changed(*id, told.changed);
    }
}

// Read over the window's own fields: what the change leaves out is what
// the window still is.
void GuestLink::read_window_changed(MessageReader& message) {
    const std::optional<WindowId> id = WindowId::read(message);
    const GuestWindow* known = id.has_value() ? windows_.find(*id) : nullptr;
    if (known == nullptr) {
        RWIN_DEBUG("link", "a change to a window not known; ignored");
        return;
    }
    WindowFields fields = known->fields();
    std::uint32_t mask = 0;
    if (!WindowFields::read(message, fields, mask)) {
        RWIN_WARN("link", "malformed change to {}", *id);
        return;
    }
    static_cast<void>(windows_.change(*id, std::move(fields)));
    RWIN_TRACE("link", "window {} changed {:#x}", *id, mask);
    window_changed(*id, mask);
}

void GuestLink::read_window_gone(MessageReader& message) {
    const std::optional<WindowId> id = WindowId::read(message);
    if (!id.has_value()) {
        RWIN_WARN("link", "malformed window gone");
        return;
    }
    if (!windows_.forget(*id)) return;
    RWIN_INFO("link", "guest says window {} is gone", *id);
    window_gone(*id);
}

void GuestLink::read_sync_end() {
    for (const WindowId gone : windows_.end_sync()) {
        RWIN_INFO("link", "window {} was not said again; it is gone", gone);
        window_gone(gone);
    }
    RWIN_INFO("link", "the guest has said its {} window(s)", windows_.size());
    sync_ended();
}

void GuestLink::read_sys_param(MessageReader& message) {
    const std::optional<SysParam> param = SysParam::read(message);
    if (!param.has_value() || param->param == SysParam::Param::DragFullWindows) {
        RWIN_WARN("link", "malformed or unknown guest setting");
        return;
    }
    const Box area = box_of(param->area);
    if (param->param == SysParam::Param::Monitor) {
        RWIN_DEBUG("link", "guest monitor {},{} {}x{}", area.x, area.y, area.width, area.height);
        monitor_changed(area);
        return;
    }
    RWIN_DEBUG("link", "guest work area {},{} {}x{}", area.x, area.y, area.width, area.height);
    work_area_changed(area);
}

void GuestLink::read_icon(MessageReader& message) {
    if (const std::optional<WindowIcon> icon = WindowIcon::read(message)) {
        window_icon(*icon);
    } else {
        RWIN_WARN("link", "malformed window icon");
    }
}

void GuestLink::read_frame(MessageReader& message) {
    const std::optional<WindowId> window = WindowId::read(message);
    const std::uint32_t width = message.u32();
    const std::uint32_t height = message.u32();
    if (!window.has_value() || !message.ok() || !valid_picture_size(width, height)) {
        RWIN_WARN("link", "invalid frame size ({}x{})", width, height);
        return;
    }
    const std::uint8_t* pixels = message.bytes(static_cast<std::size_t>(width) * height * 4);
    if (!message.ok() || pixels == nullptr) {
        RWIN_WARN("link", "truncated frame for {} ({}x{})", *window, width, height);
        return;
    }
    RWIN_TRACE("link", "frame {} {}x{}", *window, width, height);
    frame_arrived(*window, PixelsView{pixels, static_cast<int>(width), static_cast<int>(height),
                                      static_cast<int>(width * 4)});
}

void GuestLink::read_patch(MessageReader& message) {
    const std::optional<WindowId> window = WindowId::read(message);
    // Read one at a time: a function's arguments are evaluated in no set
    // order, and the height read first is a picture on its side.
    const auto width = message.u32();
    const auto height = message.u32();
    if (!window.has_value() || !message.ok() || !valid_picture_size(width, height)) {
        RWIN_WARN("link", "invalid patch picture size ({}x{})", width, height);
        return;
    }
    const Size picture{static_cast<int>(width), static_cast<int>(height)};
    const std::uint32_t count = message.u32();
    for (std::uint32_t i = 0; i < count && message.ok(); ++i) {
        const std::optional<PictureRect> rect = PictureRect::read(message);
        if (!rect.has_value()) break;
        if (!valid_picture_size(rect->width, rect->height) || rect->x > width || rect->y > height
            || rect->width > width - rect->x || rect->height > height - rect->y) {
            RWIN_WARN("link", "patch {} of {} exceeds its picture", i, *window);
            return;
        }
        const std::uint8_t* pixels = message.bytes(static_cast<std::size_t>(rect->width) * rect->height * 4);
        if (pixels == nullptr) {
            RWIN_WARN("link", "patch {} of {} is truncated", i, *window);
            return;
        }
        frame_patched(*window, picture, Point{static_cast<int>(rect->x), static_cast<int>(rect->y)},
                      PixelsView{pixels, static_cast<int>(rect->width), static_cast<int>(rect->height),
                                 static_cast<int>(rect->width * 4)});
    }
    if (!message.ok()) {
        RWIN_WARN("link", "truncated frame patch for {}", *window);
        return;
    }
    frame_patches_done(*window);
}

void GuestLink::send(std::string_view feature, const std::vector<std::uint8_t>& bytes) {
    if (!guest_.valid()) {
        RWIN_DEBUG("link", "dropping {} bytes: no guest connected", bytes.size());
        return;
    }
    if (!features_.has(feature)) {
        RWIN_DEBUG("link", "dropping message {}: the guest does not speak {}", static_cast<int>(bytes[4]), feature);
        return;
    }
    write_now(bytes.data(), bytes.size());
}

void GuestLink::send_window(std::string_view feature, MessageId id, WindowId window) {
    MessageWriter out{id};
    window.write(out);
    send(feature, out.take());
}

void GuestLink::watch_monitor() { send(Feature::kMonitor, MessageWriter{MessageId::WatchMonitor}.take()); }

void GuestLink::subscribe(WindowId window) { send_window(Feature::kWindows, MessageId::Subscribe, window); }

void GuestLink::unsubscribe(WindowId window) { send_window(Feature::kWindows, MessageId::Unsubscribe, window); }

void GuestLink::send_activate(WindowId window, bool enabled) {
    MessageWriter out{MessageId::Activate};
    Activate{window, enabled}.write(out);
    send(Feature::kWindows, out.take());
}

void GuestLink::send_sys_command(WindowId window, SysCommand::Command command) {
    MessageWriter out{MessageId::SysCommand};
    SysCommand{window, command}.write(out);
    send(Feature::kWindows, out.take());
}

void GuestLink::send_sys_menu(WindowId window, std::int32_t x, std::int32_t y) {
    MessageWriter out{MessageId::SysMenu};
    SysMenu{window, x, y}.write(out);
    send(Feature::kWindows, out.take());
}

void GuestLink::send_window_move(WindowId window, const Rect& frame) {
    MessageWriter out{MessageId::WindowMove};
    WindowMove{window, frame}.write(out);
    send(Feature::kWindows, out.take());
}

void GuestLink::send_sys_param(const SysParam& param) {
    MessageWriter out{MessageId::HostSysParam};
    param.write(out);
    send(Feature::kWindows, out.take());
}

void GuestLink::send_pointer_move(WindowId window, Point at) {
    MessageWriter out{MessageId::PointerMove};
    PointerMove{window, at.x, at.y}.write(out);
    send(Feature::kInput, out.take());
}

void GuestLink::send_pointer_button(WindowId window, Point at, std::uint8_t button, bool pressed) {
    MessageWriter out{MessageId::PointerButton};
    PointerButton{window, at.x, at.y, static_cast<PointerButton::Button>(button), pressed}.write(out);
    send(Feature::kInput, out.take());
}

void GuestLink::send_pointer_delta(WindowId window, std::int32_t dx, std::int32_t dy) {
    MessageWriter out{MessageId::PointerDelta};
    PointerDelta{window, dx, dy}.write(out);
    send(Feature::kInput, out.take());
}

void GuestLink::send_pointer_wheel(const PointerWheel& wheel) {
    MessageWriter out{MessageId::PointerWheel};
    wheel.write(out);
    send(Feature::kInput, out.take());
}

void GuestLink::send_pointer_at(const PointerAt& at) {
    MessageWriter out{MessageId::PointerAt};
    at.write(out);
    send(Feature::kInput, out.take());
}

void GuestLink::send_pointer_leave(WindowId window) { send_window(Feature::kInput, MessageId::PointerLeave, window); }

void GuestLink::send_key(const KeyStroke& stroke) {
    MessageWriter out{MessageId::Key};
    stroke.write(out);
    send(Feature::kInput, out.take());
}

void GuestLink::send_popup_dismiss(WindowId window) {
    send_window(Feature::kPopupIntent, MessageId::PopupDismiss, window);
}

void GuestLink::send_route(bool shared_surfaces) {
    MessageWriter out{MessageId::FrameRoute};
    FrameRoute{shared_surfaces}.write(out);
    send(Feature::kFramesShared, out.take());
}

void GuestLink::send_window_pixels(WindowId window) {
    send_window(Feature::kFramesShared, MessageId::WindowPixels, window);
}

void GuestLink::send_slot_free(const SlotFree& freed) {
    MessageWriter out{MessageId::SlotFree};
    freed.write(out);
    send(Feature::kFramesShared, out.take());
}

void GuestLink::send_clock_probe() {
    MessageWriter out{MessageId::ClockProbe};
    out.put_u64(static_cast<std::uint64_t>(now_ns()));
    send(Feature::kLatency, out.take());
}

}  // namespace rwin::host
