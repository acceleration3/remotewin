#include "host/mirror/mirror_session.hpp"

#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_window.hpp"

#include "logging/logger.hpp"

#include <QImage>
#include <QPoint>
#include <QSet>
#include <QTimer>

namespace rwin::host {

namespace {
// The guest pushes the window set whenever it changes, at frame rate, so
// this is only a safety net: it re-asks in case a push was lost with a
// connection, and costs one small message a second.
constexpr int kPollIntervalMs = 1000;
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
    // Every mirror is made before any is placed: a popup can arrive in
    // the same list as the window it belongs to, and ahead of it.
    QSet<quint64> present;
    QList<const GuestWindowInfo*> awaiting_placement;
    for (const GuestWindowInfo& window : windows) {
        present.insert(window.handle);
        if (auto it = mirrors_.find(window.handle); it != mirrors_.end()) {
            if (!it.value().isNull()) {
                it.value()->retitle(window.title);
                it.value()->note_guest_size(window.width, window.height);
            }
            continue;
        }
        RWIN_INFO("mirror", "new window {:#x} owner={:#x} '{}' {}x{} class='{}'",
                  window.handle, window.owner, window.title.toStdString(), window.width,
                  window.height, window.class_name.toStdString());
        auto* mirror = new MirrorWindow(link_, window);
        mirrors_.insert(window.handle, mirror);
        saw_any_ = true;
        awaiting_placement.push_back(&window);
    }

    for (const GuestWindowInfo* window : awaiting_placement) {
        auto mirror = mirrors_.value(window->handle);
        if (mirror.isNull()) continue;
        // An owner we are not mirroring is no owner at all — the window
        // stands on its own rather than being placed against nothing.
        auto owner = mirrors_.value(window->owner);
        if (mirror->is_owned() && !owner.isNull()) {
            const GuestWindowInfo* placed_against = nullptr;
            for (const GuestWindowInfo& candidate : windows) {
                if (candidate.handle == window->owner) placed_against = &candidate;
            }
            if (placed_against != nullptr) {
                mirror->place_under(*owner, QPoint(window->x - placed_against->x,
                                                   window->y - placed_against->y));
            }
        }
        mirror->show();
        link_.subscribe(window->handle);
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
    const QCursor* shape = cursor_.find(id);
    if (shape == nullptr) {
        RWIN_DEBUG("cursor", "shape {:#x} named before it arrived", id);
        return;
    }
    it.value()->wear_guest_cursor(*shape);
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
