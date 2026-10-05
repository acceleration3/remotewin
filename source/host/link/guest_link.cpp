#include "host/link/guest_link.hpp"

#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/frame_route.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/guest_desktop.hpp"
#include "proto/key_stroke.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/picture_rect.hpp"
#include "proto/pointer_delta.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/pointer_wheel.hpp"
#include "proto/slot_free.hpp"
#include "proto/wire_hello.hpp"

#include <QRect>
#include <QTcpServer>
#include <QTcpSocket>
#include <limits>
#include <optional>

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
    const bool listening = server_->listen(address, port);
    if (!listening) {
        RWIN_ERROR("link", "cannot listen on port {}: {}", port,
                   server_->errorString().toStdString());
        return false;
    }
    RWIN_INFO("link", "listening on {}:{}", address.toString().toStdString(),
              server_->serverPort());
    return true;
}

std::uint16_t GuestLink::port() const {
    return server_->serverPort();
}

void GuestLink::accept_guest() {
    QTcpSocket* incoming = server_->nextPendingConnection();
    if (incoming == nullptr)
        return;
    RWIN_DEBUG("link", "connection from {}", incoming->peerAddress().toString().toStdString());
    if (guest_ != nullptr) {
        RWIN_WARN("link", "a guest is already connected; refusing the second");
        // One guest per link. A second dial-in is a stale process that
        // has not noticed it was replaced.
        incoming->close();
        incoming->deleteLater();
        return;
    }
    guest_ = incoming;
    greeted_ = false;
    decoder_ = FrameDecoder{};
    guest_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(guest_, &QTcpSocket::readyRead, this, &GuestLink::read_available);
    connect(guest_, &QTcpSocket::disconnected, this, [this] {
        RWIN_INFO("link", "guest disconnected");
        guest_->deleteLater();
        guest_ = nullptr;
        Q_EMIT disconnected();
    });
    RWIN_INFO("link", "guest dialled in from {}", guest_->peerAddress().toString().toStdString());
    MessageWriter hello{MessageId::HostHello};
    WireHello{}.write(hello);
    send(hello.take());
}

void GuestLink::greet(const std::uint8_t* payload, std::size_t size) {
    MessageReader message{payload, size};
    QString reason;
    if (message.id() != MessageId::GuestHello) {
        reason = QStringLiteral("the guest half's first message is %1, not its hello: it is older "
                                "than this remotewin (wire %2). Put the remotewin-guest.exe built "
                                "with this remotewin in the guest")
                     .arg(static_cast<int>(message.id()))
                     .arg(WireHello::kVersion);
    } else if (const std::optional<WireHello> hello = WireHello::read(message);
               !hello.has_value() || hello->version != WireHello::kVersion) {
        reason = QStringLiteral("the guest half speaks wire %1, this remotewin wire %2: the two "
                                "halves are from different builds. Put the remotewin-guest.exe "
                                "built with this remotewin in the guest")
                     .arg(hello.has_value() ? hello->version : 0)
                     .arg(WireHello::kVersion);
    }
    if (!reason.isEmpty()) {
        RWIN_ERROR("link", "{}", reason.toStdString());
        guest_->close();
        Q_EMIT mismatched(reason);
        return;
    }
    RWIN_INFO("link", "the guest half speaks wire {} too", WireHello::kVersion);
    greeted_ = true;
    Q_EMIT connected();
}

void GuestLink::read_available() {
    const QByteArray chunk = guest_->readAll();
    decoder_.feed(reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                  static_cast<std::size_t>(chunk.size()));

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
    switch (message.id()) {
    case MessageId::WindowList: {
        const bool application_alive = message.u8() != 0;
        const std::optional<GuestDesktop> desktop = GuestDesktop::read(message);
        if (!desktop.has_value()) {
            RWIN_WARN("link", "window list without its desktop");
            break;
        }
        const QSize guest_room(static_cast<int>(desktop->room_width),
                               static_cast<int>(desktop->room_height));
        const std::uint32_t count = message.u32();
        // Each entry needs at least 70 bytes, even with empty strings/region.
        if (!message.ok() || count > message.remaining() / 70) {
            RWIN_WARN("link", "invalid window count {}", count);
            break;
        }
        QList<GuestWindowInfo> windows;
        windows.reserve(static_cast<qsizetype>(count));
        for (std::uint32_t i = 0; i < count && message.ok(); ++i) {
            GuestWindowInfo window;
            window.handle = message.u64();
            window.owner = message.u64();
            window.process_id = message.u32();
            window.x = message.i32();
            window.y = message.i32();
            window.width = message.u32();
            window.height = message.u32();
            window.style = message.u32();
            window.extended_style = message.u32();
            window.class_style = message.u32();
            window.title = QString::fromStdString(message.string());
            window.class_name = QString::fromStdString(message.string());
            const std::uint8_t state = message.u8();
            window.minimized = (state & 1) != 0;
            window.maximized = (state & 2) != 0;
            window.dwm_frame = (state & 4) != 0;
            window.foreground = (state & 8) != 0;
            window.layered_flags = message.u32();
            window.layered_alpha = message.u8();
            window.color_key = message.u32();
            window.band = message.u32();
            window.drop_shadows = desktop->drop_shadows;
            const std::uint32_t rects = message.u32();
            if (rects > message.remaining() / 16) {
                RWIN_WARN("link", "invalid region count {} for {:#x}", rects, window.handle);
                return;
            }
            for (std::uint32_t r = 0; r < rects && message.ok(); ++r) {
                const std::int32_t left = message.i32();
                const std::int32_t top = message.i32();
                const std::int32_t right = message.i32();
                const std::int32_t bottom = message.i32();
                if (right <= left || bottom <= top ||
                    static_cast<std::int64_t>(right) - left > std::numeric_limits<int>::max() ||
                    static_cast<std::int64_t>(bottom) - top > std::numeric_limits<int>::max()) {
                    RWIN_WARN("link", "invalid region rectangle for {:#x}", window.handle);
                    return;
                }
                window.region.push_back(QRect(QPoint(left, top), QPoint(right - 1, bottom - 1)));
            }
            windows.push_back(std::move(window));
        }
        if (!message.ok()) {
            RWIN_WARN("link", "truncated window list of {}", count);
            break;
        }
        RWIN_DEBUG("link", "window list: {}", windows.size());
        for (const GuestWindowInfo& w : windows) {
            RWIN_DEBUG("link",
                       "  {:#x} owner={:#x} {}x{} at {},{} style={:#010x} "
                       "ex={:#010x} class='{}' title='{}'",
                       w.handle, w.owner, w.width, w.height, w.x, w.y, w.style, w.extended_style,
                       w.class_name.toStdString(), w.title.toStdString());
        }
        Q_EMIT windows_listed(windows, application_alive, guest_room);
        break;
    }
    case MessageId::Frame: {
        const std::uint64_t window = message.u64();
        const std::uint32_t width = message.u32();
        const std::uint32_t height = message.u32();
        if (!message.ok() || !valid_picture_size(width, height)) {
            RWIN_WARN("link", "invalid frame size for {:#x} ({}x{})", window, width, height);
            break;
        }
        const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
        const std::uint8_t* pixels = message.bytes(bytes);
        if (!message.ok() || pixels == nullptr) {
            RWIN_WARN("link", "truncated frame for {:#x} ({}x{})", window, width, height);
            break;
        }
        RWIN_TRACE("link", "frame {:#x} {}x{}", window, width, height);
        // Copied out of the decoder's buffer, which the next read
        // invalidates.
        // Premultiplied, because that is how the compositor composed
        // it: a window with rounded corners, a shadow, or one that is
        // deliberately see-through arrives with the alpha that says
        // so, and reading it as RGB32 would paint all of that black.
        const QImage frame(pixels, static_cast<int>(width), static_cast<int>(height),
                           static_cast<int>(width * 4), QImage::Format_ARGB32_Premultiplied);
        Q_EMIT frame_arrived(window, frame.copy());
        break;
    }
    case MessageId::FramePatch:
        read_patch(message);
        break;
    case MessageId::SlotRing: {
        const std::optional<SlotRing> ring = SlotRing::read(message);
        if (!ring.has_value()) {
            RWIN_WARN("link", "malformed slot ring");
            break;
        }
        Q_EMIT ring_announced(*ring);
        break;
    }
    case MessageId::FrameInSlot: {
        const std::optional<FrameInSlot> frame = FrameInSlot::read(message);
        if (!frame.has_value()) {
            RWIN_WARN("link", "malformed frame in slot");
            break;
        }
        RWIN_TRACE("link", "frame {:#x} in slot {} of ring {}, copy {}", frame->window, frame->slot,
                   frame->ring, frame->copies);
        Q_EMIT frame_in_slot(*frame);
        break;
    }
    case MessageId::WindowGone:
        if (const std::uint64_t window = message.u64(); message.ok()) {
            RWIN_INFO("link", "guest says window {:#x} is gone", window);
            Q_EMIT window_gone(window);
        }
        break;
    case MessageId::DragStarted: {
        const std::uint64_t window = message.u64();
        if (!message.ok())
            break;
        RWIN_INFO("link", "guest handed us a drag of {:#x}", window);
        Q_EMIT drag_started(window);
        break;
    }
    case MessageId::CursorShape: {
        const std::optional<CursorPicture> picture = CursorPicture::read(message);
        if (!picture.has_value()) {
            RWIN_WARN("link", "malformed cursor picture");
            break;
        }
        Q_EMIT cursor_pictured(*picture);
        break;
    }
    case MessageId::CursorChanged: {
        const std::optional<GuestCursor> cursor = GuestCursor::read(message);
        if (cursor.has_value())
            Q_EMIT cursor_changed(*cursor);
        break;
    }
    case MessageId::PointerGrabbed: {
        const std::optional<PointerGrab> grab = PointerGrab::read(message);
        if (!grab.has_value()) {
            RWIN_WARN("link", "malformed pointer grab");
            break;
        }
        Q_EMIT pointer_grabbed(*grab);
        break;
    }
    default:
        RWIN_WARN("link", "unknown message id {}", static_cast<int>(message.id()));
        break;
    }
}

void GuestLink::read_patch(MessageReader& message) {
    const std::uint64_t window = message.u64();
    // Read one at a time: a function's arguments are evaluated in no set
    // order, and the height read first is a picture on its side.
    const auto width = message.u32();
    const auto height = message.u32();
    if (!message.ok() || !valid_picture_size(width, height)) {
        RWIN_WARN("link", "invalid patch picture size for {:#x} ({}x{})", window, width, height);
        return;
    }
    const QSize picture(static_cast<int>(width), static_cast<int>(height));
    const std::uint32_t count = message.u32();
    RWIN_TRACE("link", "patch {:#x}: {} rect(s)", window, count);
    const QRect whole(QPoint(0, 0), picture);
    for (std::uint32_t i = 0; i < count && message.ok(); ++i) {
        const std::optional<PictureRect> rect = PictureRect::read(message);
        if (!rect.has_value())
            break;
        if (!valid_picture_size(rect->width, rect->height) || rect->x > width || rect->y > height ||
            rect->width > width - rect->x || rect->height > height - rect->y) {
            RWIN_WARN("link", "patch {} of {:#x} exceeds its picture", i, window);
            return;
        }
        const QRect where(static_cast<int>(rect->x), static_cast<int>(rect->y),
                          static_cast<int>(rect->width), static_cast<int>(rect->height));
        const std::uint8_t* pixels =
            message.bytes(static_cast<std::size_t>(rect->width) * rect->height * 4);
        if (pixels == nullptr || where.isEmpty() || !whole.contains(where)) {
            RWIN_WARN("link", "patch {} of {:#x} is outside its {}x{} picture or truncated", i,
                      window, picture.width(), picture.height());
            return;
        }
        // Copied out of the decoder's buffer, which the next read
        // invalidates; premultiplied, as a whole frame is.
        const QImage patch(pixels, where.width(), where.height(), where.width() * 4,
                           QImage::Format_ARGB32_Premultiplied);
        Q_EMIT frame_patched(window, picture, where.topLeft(), patch.copy());
    }
    if (!message.ok())
        RWIN_WARN("link", "truncated frame patch for {:#x}", window);
}

void GuestLink::send(std::vector<std::uint8_t> bytes) {
    if (guest_ == nullptr) {
        RWIN_DEBUG("link", "dropping {} bytes: no guest connected", bytes.size());
        return;
    }
    guest_->write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
}

void GuestLink::watch_scope(std::uint32_t process_id, const QString& job_name) {
    MessageWriter out{MessageId::WatchScope};
    out.put_u32(process_id);
    out.put_string(job_name.toStdString());
    send(out.take());
}

void GuestLink::watch_monitor() {
    send(MessageWriter{MessageId::WatchMonitor}.take());
}

void GuestLink::subscribe(std::uint64_t window) {
    MessageWriter out{MessageId::Subscribe};
    out.put_u64(window);
    send(out.take());
}

void GuestLink::unsubscribe(std::uint64_t window) {
    MessageWriter out{MessageId::Unsubscribe};
    out.put_u64(window);
    send(out.take());
}

void GuestLink::send_pointer_move(std::uint64_t window, std::int32_t x, std::int32_t y) {
    MessageWriter out{MessageId::PointerMove};
    out.put_u64(window);
    out.put_i32(x);
    out.put_i32(y);
    send(out.take());
}

void GuestLink::send_pointer_button(std::uint64_t window, std::int32_t x, std::int32_t y,
                                    std::uint8_t button, bool pressed) {
    MessageWriter out{MessageId::PointerButton};
    out.put_u64(window);
    out.put_i32(x);
    out.put_i32(y);
    out.put_u8(button);
    out.put_u8(pressed ? 1 : 0);
    send(out.take());
}

void GuestLink::send_pointer_wheel(const PointerWheel& wheel) {
    MessageWriter out{MessageId::PointerWheel};
    wheel.write(out);
    send(out.take());
}

void GuestLink::send_pointer_delta(std::uint64_t window, std::int32_t dx, std::int32_t dy) {
    MessageWriter out{MessageId::PointerDelta};
    PointerDelta{window, dx, dy}.write(out);
    send(out.take());
}

void GuestLink::send_key(const KeyStroke& stroke) {
    MessageWriter out{MessageId::Key};
    stroke.write(out);
    send(out.take());
}

void GuestLink::send_close(std::uint64_t window) {
    MessageWriter out{MessageId::CloseWindow};
    out.put_u64(window);
    send(out.take());
}

void GuestLink::send_focus(std::uint64_t window) {
    MessageWriter out{MessageId::FocusWindow};
    out.put_u64(window);
    send(out.take());
}

void GuestLink::send_resize(std::uint64_t window, std::uint32_t width, std::uint32_t height) {
    MessageWriter out{MessageId::ResizeWindow};
    out.put_u64(window);
    out.put_u32(width);
    out.put_u32(height);
    send(out.take());
}

void GuestLink::send_route(bool shared_surfaces) {
    MessageWriter out{MessageId::FrameRoute};
    FrameRoute{shared_surfaces}.write(out);
    send(out.take());
}

void GuestLink::send_slot_free(const SlotFree& freed) {
    MessageWriter out{MessageId::SlotFree};
    freed.write(out);
    send(out.take());
}

void GuestLink::send_move(std::uint64_t window, std::int32_t x, std::int32_t y) {
    MessageWriter out{MessageId::MoveWindow};
    out.put_u64(window);
    out.put_i32(x);
    out.put_i32(y);
    send(out.take());
}

}  // namespace rwin::host
