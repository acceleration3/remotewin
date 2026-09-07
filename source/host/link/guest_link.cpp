#include "host/link/guest_link.hpp"

#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

#include "logging/logger.hpp"

#include <QTcpServer>
#include <QTcpSocket>

namespace rwin::host {

GuestLink::GuestLink(QObject* parent)
    : QObject(parent), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &GuestLink::accept_guest);
}

bool GuestLink::listen(std::uint16_t port) {
    // AnyIPv4 rather than Any: QEMU's user networking forwards a guest's
    // connection to 10.0.2.2 onto the host's IPv4 loopback, and a
    // v6-only bind refuses it with no hint as to why.
    const bool listening = server_->listen(QHostAddress::AnyIPv4, port);
    if (!listening) {
        RWIN_ERROR("link", "cannot listen on port {}: {}", port,
                   server_->errorString().toStdString());
        return false;
    }
    RWIN_INFO("link", "listening on 0.0.0.0:{}", port);
    return true;
}

void GuestLink::accept_guest() {
    QTcpSocket* incoming = server_->nextPendingConnection();
    if (incoming == nullptr) return;
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
    guest_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(guest_, &QTcpSocket::readyRead, this, &GuestLink::read_available);
    connect(guest_, &QTcpSocket::disconnected, this, [this] {
        RWIN_INFO("link", "guest disconnected");
        guest_->deleteLater();
        guest_ = nullptr;
        Q_EMIT disconnected();
    });
    RWIN_INFO("link", "guest dialled in from {}",
              guest_->peerAddress().toString().toStdString());
    Q_EMIT connected();
}

void GuestLink::read_available() {
    const QByteArray chunk = guest_->readAll();
    decoder_.feed(reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                  static_cast<std::size_t>(chunk.size()));

    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    while (decoder_.next(&payload, &size)) {
        dispatch(payload, size);
    }
    if (decoder_.overflowed()) {
        RWIN_ERROR("link", "guest framing is broken; closing");
        guest_->close();
    }
}

void GuestLink::dispatch(const std::uint8_t* payload, std::size_t size) {
    MessageReader message{payload, size};
    switch (message.id()) {
        case MessageId::WindowList: {
            const bool application_alive = message.u8() != 0;
            const std::uint32_t count = message.u32();
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
                const std::uint32_t rects = message.u32();
                for (std::uint32_t r = 0; r < rects && message.ok(); ++r) {
                    const std::int32_t left = message.i32();
                    const std::int32_t top = message.i32();
                    const std::int32_t right = message.i32();
                    const std::int32_t bottom = message.i32();
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
                           w.handle, w.owner, w.width, w.height, w.x, w.y, w.style,
                           w.extended_style, w.class_name.toStdString(),
                           w.title.toStdString());
            }
            Q_EMIT windows_listed(windows, application_alive);
            break;
        }
        case MessageId::Frame: {
            const std::uint64_t window = message.u64();
            const std::uint32_t width = message.u32();
            const std::uint32_t height = message.u32();
            const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
            const std::uint8_t* pixels = message.bytes(bytes);
            if (!message.ok() || pixels == nullptr) {
                RWIN_WARN("link", "truncated frame for {:#x} ({}x{})", window, width,
                          height);
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
                               static_cast<int>(width * 4),
                               QImage::Format_ARGB32_Premultiplied);
            Q_EMIT frame_arrived(window, frame.copy());
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
            if (!message.ok()) break;
            RWIN_INFO("link", "guest handed us a drag of {:#x}", window);
            Q_EMIT drag_started(window);
            break;
        }
        case MessageId::CursorShape: {
            const std::uint64_t id = message.u64();
            const std::uint32_t width = message.u32();
            const std::uint32_t height = message.u32();
            const std::uint32_t hotspot_x = message.u32();
            const std::uint32_t hotspot_y = message.u32();
            const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
            const std::uint8_t* pixels = message.bytes(bytes);
            if (!message.ok() || pixels == nullptr) {
                RWIN_WARN("link", "truncated cursor {:#x} ({}x{})", id, width, height);
                break;
            }
            Q_EMIT cursor_shape(id, width, height, hotspot_x, hotspot_y,
                                QByteArray(reinterpret_cast<const char*>(pixels),
                                           static_cast<qsizetype>(bytes)));
            break;
        }
        case MessageId::CursorChanged: {
            const std::uint64_t id = message.u64();
            if (message.ok()) Q_EMIT cursor_changed(id);
            break;
        }
        default:
            RWIN_WARN("link", "unknown message id {}", static_cast<int>(message.id()));
            break;
    }
}

void GuestLink::send(std::vector<std::uint8_t> bytes) {
    if (guest_ == nullptr) {
        RWIN_DEBUG("link", "dropping {} bytes: no guest connected", bytes.size());
        return;
    }
    guest_->write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<qint64>(bytes.size()));
}

void GuestLink::watch_scope(std::uint32_t process_id, const QString& job_name) {
    MessageWriter out{MessageId::WatchScope};
    out.put_u32(process_id);
    out.put_string(job_name.toStdString());
    send(out.take());
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

void GuestLink::send_key(std::uint64_t window, std::uint32_t virtual_key,
                         std::uint32_t character, bool pressed) {
    MessageWriter out{MessageId::Key};
    out.put_u64(window);
    out.put_u32(virtual_key);
    out.put_u32(character);
    out.put_u8(pressed ? 1 : 0);
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

void GuestLink::send_window_state(std::uint64_t window, bool minimized, bool maximized) {
    MessageWriter out{MessageId::WindowState};
    out.put_u64(window);
    out.put_u8(static_cast<std::uint8_t>((minimized ? 1 : 0) | (maximized ? 2 : 0)));
    send(out.take());
}

void GuestLink::send_resize(std::uint64_t window, std::uint32_t width,
                            std::uint32_t height) {
    MessageWriter out{MessageId::ResizeWindow};
    out.put_u64(window);
    out.put_u32(width);
    out.put_u32(height);
    send(out.take());
}

}  // namespace rwin::host
