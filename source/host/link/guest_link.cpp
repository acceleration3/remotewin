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

#include <QTcpServer>
#include <QTcpSocket>

#include <limits>
#include <optional>
#include <utility>

namespace rwin::host {

namespace {

bool valid_picture_size(std::uint32_t width, std::uint32_t height) {
    constexpr auto kLargestStride = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    return width != 0 && height != 0 && width <= kLargestStride / 4 && height <= kLargestStride;
}

}  // namespace

GuestLink::GuestLink(QObject* parent) : QObject(parent), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &GuestLink::accept_guest);
}

bool GuestLink::listen(std::uint16_t port, const QHostAddress& address) {
    // AnyIPv4 rather than Any: QEMU's user networking forwards a guest's
    // connection to 10.0.2.2 onto the host's IPv4 loopback, and a
    // v6-only bind refuses it with no hint as to why.
    if (!server_->listen(address, port)) {
        RWIN_ERROR("link", "cannot listen on port {}: {}", port, server_->errorString().toStdString());
        return false;
    }
    RWIN_INFO("link", "listening on {}:{}", address.toString().toStdString(), server_->serverPort());
    return true;
}

std::uint16_t GuestLink::port() const { return server_->serverPort(); }

void GuestLink::accept_guest() {
    QTcpSocket* incoming = server_->nextPendingConnection();
    if (incoming == nullptr) return;
    if (guest_ != nullptr) {
        // One guest per link. A second dial-in is a stale process that
        // has not noticed it was replaced.
        RWIN_WARN("link", "a guest is already connected; refusing the second");
        incoming->close();
        incoming->deleteLater();
        return;
    }
    guest_ = incoming;
    greeted_ = false;
    features_ = FeatureSet{};
    windows_ = GuestWindows{};
    decoder_ = FrameDecoder{};
    guest_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(guest_, &QTcpSocket::readyRead, this, &GuestLink::read_available);
    connect(guest_, &QTcpSocket::disconnected, this, [this] {
        RWIN_INFO("link", "guest disconnected");
        guest_->deleteLater();
        guest_ = nullptr;
        features_ = FeatureSet{};
        Q_EMIT disconnected();
    });
    RWIN_INFO("link", "guest dialled in from {}", guest_->peerAddress().toString().toStdString());
    MessageWriter hello{MessageId::HostHello};
    Hello::ours().write(hello);
    const std::vector<std::uint8_t> bytes = hello.take();
    guest_->write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
}

void GuestLink::greet(const std::uint8_t* payload, std::size_t size) {
    MessageReader message{payload, size};
    if (message.id() != MessageId::GuestHello) {
        refuse(QStringLiteral("the guest half's first message is %1, not its hello: it is from another "
                              "build than this remotewin. Put the remotewin-guest.exe built with this "
                              "remotewin in the guest")
                   .arg(static_cast<int>(message.id())));
        return;
    }
    const std::optional<Hello> hello = Hello::read(message);
    if (!hello.has_value()) {
        refuse(QStringLiteral("the guest half's hello is malformed: it is from another build than this "
                              "remotewin. Put the remotewin-guest.exe built with this remotewin in the guest"));
        return;
    }
    const FeatureSet features{Hello::ours().features, hello->features};
    if (const std::string refusal = features.refusal(); !refusal.empty()) {
        refuse(QString::fromStdString(refusal));
        return;
    }
    features_ = features;
    greeted_ = true;
    RWIN_INFO("link", "the guest half is '{}'; speaking {}", hello->build, features_.describe());
    Q_EMIT connected();
}

void GuestLink::refuse(const QString& reason) {
    RWIN_ERROR("link", "{}", reason.toStdString());
    guest_->close();
    Q_EMIT mismatched(reason);
}

void GuestLink::read_available() {
    const QByteArray chunk = guest_->readAll();
    decoder_.feed(reinterpret_cast<const std::uint8_t*>(chunk.constData()), static_cast<std::size_t>(chunk.size()));
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    while (guest_ != nullptr && guest_->isOpen() && decoder_.next(&payload, &size)) {
        if (greeted_) {
            dispatch(payload, size);
        } else {
            greet(payload, size);
        }
    }
    if (guest_ != nullptr && decoder_.overflowed()) {
        RWIN_ERROR("link", "guest framing is broken; closing");
        guest_->close();
    }
}

void GuestLink::dispatch(const std::uint8_t* payload, std::size_t size) {
    MessageReader message{payload, size};
    if (dispatch_windows(message) || dispatch_frames(message) || dispatch_pointer(message)) return;
    if (message.id() == MessageId::ClockEcho) {
        if (const std::optional<ClockEcho> echo = ClockEcho::read(message)) {
            Q_EMIT clock_echoed(*echo, now_ns());
        } else {
            RWIN_WARN("link", "malformed clock echo");
        }
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
                Q_EMIT sync_began();
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
                Q_EMIT restacked(*order);
            } else {
                RWIN_WARN("link", "malformed z-order");
            }
            return true;
        case MessageId::MinMax:
            if (const std::optional<MinMax> limits = MinMax::read(message)) {
                if (windows_.limit(*limits)) Q_EMIT window_limited(limits->window);
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
                Q_EMIT ring_announced(*ring);
            } else {
                RWIN_WARN("link", "malformed slot ring");
            }
            return true;
        case MessageId::FrameInSlot:
            if (const std::optional<FrameInSlot> frame = FrameInSlot::read(message)) {
                RWIN_TRACE("link", "frame {} in slot {} of ring {}, copy {}", frame->window, frame->slot,
                           frame->ring, frame->copies);
                Q_EMIT frame_in_slot(*frame);
            } else {
                RWIN_WARN("link", "malformed frame in slot");
            }
            return true;
        case MessageId::SurfaceLost:
            if (const std::optional<SurfaceLost> lost = SurfaceLost::read(message)) {
                RWIN_INFO("link", "the guest lost {}'s frames", lost->window);
                Q_EMIT surface_lost(lost->window);
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
                Q_EMIT move_size_started(*start);
            } else {
                RWIN_WARN("link", "malformed move/size start");
            }
            return true;
        case MessageId::MoveSizeEnd:
            if (const std::optional<MoveSizeEnd> end = MoveSizeEnd::read(message)) {
                Q_EMIT move_size_ended(*end);
            } else {
                RWIN_WARN("link", "malformed move/size end");
            }
            return true;
        case MessageId::CursorShape:
            if (const std::optional<CursorPicture> picture = CursorPicture::read(message)) {
                Q_EMIT cursor_pictured(*picture);
            } else {
                RWIN_WARN("link", "malformed cursor picture");
            }
            return true;
        case MessageId::CursorChanged:
            if (const std::optional<GuestCursor> cursor = GuestCursor::read(message)) Q_EMIT cursor_changed(*cursor);
            return true;
        case MessageId::PointerGrab:
            if (const std::optional<PointerGrab> grab = PointerGrab::read(message)) {
                Q_EMIT pointer_grabbed(*grab);
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
        Q_EMIT window_new(*id);
    } else if (told.changed != 0) {
        Q_EMIT window_changed(*id, told.changed);
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
    Q_EMIT window_changed(*id, mask);
}

void GuestLink::read_window_gone(MessageReader& message) {
    const std::optional<WindowId> id = WindowId::read(message);
    if (!id.has_value()) {
        RWIN_WARN("link", "malformed window gone");
        return;
    }
    if (!windows_.forget(*id)) return;
    RWIN_INFO("link", "guest says window {} is gone", *id);
    Q_EMIT window_gone(*id);
}

void GuestLink::read_sync_end() {
    for (const WindowId gone : windows_.end_sync()) {
        RWIN_INFO("link", "window {} was not said again; it is gone", gone);
        Q_EMIT window_gone(gone);
    }
    RWIN_INFO("link", "the guest has said its {} window(s)", windows_.size());
    Q_EMIT sync_ended();
}

void GuestLink::read_sys_param(MessageReader& message) {
    const std::optional<SysParam> param = SysParam::read(message);
    if (!param.has_value() || param->param != SysParam::Param::WorkArea) {
        RWIN_WARN("link", "malformed or unknown guest setting");
        return;
    }
    const Rect& area = param->work_area;
    RWIN_DEBUG("link", "guest work area {},{} {}x{}", area.x, area.y, area.width, area.height);
    Q_EMIT work_area_changed(QRect(area.x, area.y, static_cast<int>(area.width), static_cast<int>(area.height)));
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
    // Copied out of the decoder's buffer, which the next read invalidates.
    // Premultiplied, because that is how the compositor composed it: a
    // window with rounded corners, a shadow, or one that is deliberately
    // see-through arrives with the alpha that says so, and reading it as
    // RGB32 would paint all of that black.
    const QImage frame(pixels, static_cast<int>(width), static_cast<int>(height), static_cast<int>(width * 4),
                       QImage::Format_ARGB32_Premultiplied);
    Q_EMIT frame_arrived(*window, frame.copy());
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
    const QSize picture(static_cast<int>(width), static_cast<int>(height));
    const std::uint32_t count = message.u32();
    const QRect whole(QPoint(0, 0), picture);
    for (std::uint32_t i = 0; i < count && message.ok(); ++i) {
        const std::optional<PictureRect> rect = PictureRect::read(message);
        if (!rect.has_value()) break;
        if (!valid_picture_size(rect->width, rect->height) || rect->x > width || rect->y > height
            || rect->width > width - rect->x || rect->height > height - rect->y) {
            RWIN_WARN("link", "patch {} of {} exceeds its picture", i, *window);
            return;
        }
        const QRect where(static_cast<int>(rect->x), static_cast<int>(rect->y), static_cast<int>(rect->width),
                          static_cast<int>(rect->height));
        const std::uint8_t* pixels = message.bytes(static_cast<std::size_t>(rect->width) * rect->height * 4);
        if (pixels == nullptr || where.isEmpty() || !whole.contains(where)) {
            RWIN_WARN("link", "patch {} of {} is outside its {}x{} picture or truncated", i, *window,
                      picture.width(), picture.height());
            return;
        }
        // Copied out of the decoder's buffer; premultiplied, as a whole
        // frame is.
        const QImage patch(pixels, where.width(), where.height(), where.width() * 4,
                           QImage::Format_ARGB32_Premultiplied);
        Q_EMIT frame_patched(*window, picture, where.topLeft(), patch.copy());
    }
    if (!message.ok()) RWIN_WARN("link", "truncated frame patch for {}", *window);
}

void GuestLink::send(std::string_view feature, std::vector<std::uint8_t> bytes) {
    if (guest_ == nullptr) {
        RWIN_DEBUG("link", "dropping {} bytes: no guest connected", bytes.size());
        return;
    }
    if (!features_.has(feature)) {
        RWIN_DEBUG("link", "dropping message {}: the guest does not speak {}", static_cast<int>(bytes[4]), feature);
        return;
    }
    guest_->write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
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

void GuestLink::send_pointer_move(WindowId window, std::int32_t x, std::int32_t y) {
    MessageWriter out{MessageId::PointerMove};
    PointerMove{window, x, y}.write(out);
    send(Feature::kInput, out.take());
}

void GuestLink::send_pointer_button(WindowId window, std::int32_t x, std::int32_t y, std::uint8_t button,
                                    bool pressed) {
    MessageWriter out{MessageId::PointerButton};
    PointerButton{window, x, y, static_cast<PointerButton::Button>(button), pressed}.write(out);
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

void GuestLink::send_pointer_leave(WindowId window) {
    send_window(Feature::kInput, MessageId::PointerLeave, window);
}

void GuestLink::send_key(const KeyStroke& stroke) {
    MessageWriter out{MessageId::Key};
    stroke.write(out);
    send(Feature::kInput, out.take());
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
