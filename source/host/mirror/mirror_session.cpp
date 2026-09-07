#include "host/mirror/mirror_session.hpp"

#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_window.hpp"

#include "logging/logger.hpp"

#include <QImage>
#include <QTimer>

namespace rwin::host {

namespace {
// How often the window set is re-asked for. Windows opening and closing
// is not frequent enough to deserve a push channel yet; frames have one.
constexpr int kPollIntervalMs = 500;
}  // namespace

MirrorSession::MirrorSession(GuestLink& link, std::uint32_t process_id, QObject* parent)
    : QObject(parent), link_(link), process_id_(process_id), poll_(new QTimer(this)) {
    connect(&link_, &GuestLink::windows_listed, this, &MirrorSession::refresh);
    connect(&link_, &GuestLink::frame_arrived, this, &MirrorSession::show_frame);
    connect(&link_, &GuestLink::window_gone, this, &MirrorSession::forget);
    connect(&link_, &GuestLink::non_client_hit, this, &MirrorSession::note_hit);
    connect(&link_, &GuestLink::cursor_changed, this, &MirrorSession::show_cursor);
    connect(&link_, &GuestLink::cursor_shape, this,
            [this](quint64 id, quint32 width, quint32 height, quint32 hotspot_x,
                   quint32 hotspot_y, const QByteArray& pixels) {
        cursor_.remember(id, width, height, hotspot_x, hotspot_y,
                         reinterpret_cast<const std::uint8_t*>(pixels.constData()));
    });

    poll_->setInterval(kPollIntervalMs);
    connect(poll_, &QTimer::timeout, this,
            [this] { link_.request_window_list(process_id_); });
    poll_->start();
    link_.request_window_list(process_id_);
}

void MirrorSession::refresh(const QList<GuestWindowInfo>& windows) {
    QSet<quint64> present;
    for (const GuestWindowInfo& window : windows) {
        present.insert(window.handle);
        if (auto it = mirrors_.find(window.handle); it != mirrors_.end()) {
            if (!it.value().isNull()) {
                it.value()->retitle(window.title);
                it.value()->note_guest_size(window.width, window.height);
            }
            continue;
        }
        RWIN_INFO("mirror", "new window {:#x} '{}' {}x{} class='{}'", window.handle,
                  window.title.toStdString(), window.width, window.height,
                  window.class_name.toStdString());
        auto* mirror = new MirrorWindow(link_, window);
        mirrors_.insert(window.handle, mirror);
        saw_any_ = true;
        mirror->show();
        link_.subscribe(window.handle);
    }

    for (auto it = mirrors_.begin(); it != mirrors_.end();) {
        if (present.contains(it.key()) && !it.value().isNull()) {
            ++it;
            continue;
        }
        RWIN_INFO("mirror", "window {:#x} left the list; closing its mirror", it.key());
        if (!it.value().isNull()) {
            it.value()->guest_closed();
        }
        link_.unsubscribe(it.key());
        it = mirrors_.erase(it);
    }

    if (saw_any_ && mirrors_.isEmpty()) {
        RWIN_INFO("mirror", "every window closed; session is done");
        Q_EMIT finished();
    }
}

void MirrorSession::note_hit(quint64 window, qint32 hit_code) {
    pointer_over_ = window;
    if (auto it = mirrors_.find(window); it != mirrors_.end() && !it.value().isNull()) {
        it.value()->note_hit(hit_code);
    }
}

void MirrorSession::show_cursor(quint64 id) {
    auto it = mirrors_.find(pointer_over_);
    if (it == mirrors_.end() || it.value().isNull()) return;
    if (!cursor_.apply(id, *it.value())) {
        RWIN_DEBUG("cursor", "shape {:#x} named before it arrived", id);
    }
}

void MirrorSession::show_frame(quint64 window, const QImage& pixels) {
    if (auto it = mirrors_.find(window); it != mirrors_.end() && !it.value().isNull()) {
        it.value()->present(pixels);
    }
}

void MirrorSession::forget(quint64 window) {
    auto it = mirrors_.find(window);
    if (it == mirrors_.end()) return;
    if (!it.value().isNull()) it.value()->guest_closed();
    mirrors_.erase(it);
    if (saw_any_ && mirrors_.isEmpty()) {
        Q_EMIT finished();
    }
}

}  // namespace rwin::host
