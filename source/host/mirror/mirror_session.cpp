#include "host/mirror/mirror_session.hpp"

#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_window.hpp"

#include "logging/logger.hpp"

#include <QImage>
#include <QPoint>
#include <QTimer>

#include <utility>

namespace rwin::host {

namespace {
// The guest pushes the window set whenever it changes, at frame rate, so
// this is only a safety net: it re-asks in case a push was lost with a
// connection, and costs one small message a second.
constexpr int kPollIntervalMs = 1000;
}  // namespace

MirrorSession::MirrorSession(GuestLink& link, std::uint32_t process_id,
                             QString job_name, QObject* parent)
    : QObject(parent),
      link_(link),
      process_id_(process_id),
      job_name_(std::move(job_name)),
      poll_(new QTimer(this)) {
    connect(&link_, &GuestLink::windows_listed, this, &MirrorSession::refresh);
    connect(&link_, &GuestLink::frame_arrived, this, &MirrorSession::show_frame);
    connect(&link_, &GuestLink::window_gone, this, &MirrorSession::forget);
    connect(&link_, &GuestLink::drag_started, this, &MirrorSession::hand_over_drag);
    connect(&link_, &GuestLink::cursor_changed, this, &MirrorSession::show_cursor);
    connect(&link_, &GuestLink::cursor_shape, this,
            [this](quint64 id, quint32 width, quint32 height, quint32 hotspot_x,
                   quint32 hotspot_y, const QByteArray& pixels) {
        cursor_.remember(id, width, height, hotspot_x, hotspot_y,
                         reinterpret_cast<const std::uint8_t*>(pixels.constData()));
    });

    poll_->setInterval(kPollIntervalMs);
    connect(poll_, &QTimer::timeout, this,
            [this] { link_.watch_scope(process_id_, job_name_); });
    poll_->start();
    link_.watch_scope(process_id_, job_name_);
}

void MirrorSession::refresh(const QList<GuestWindowInfo>& windows,
                            bool application_alive) {
    application_alive_ = application_alive;
    Listed listed;
    listed.reserve(windows.size());
    QList<const GuestWindowInfo*> arrivals;
    for (const GuestWindowInfo& window : windows) {
        listed.insert(window.handle, &window);
        if (!mirrors_.contains(window.handle) || mirrors_.value(window.handle).isNull()) {
            arrivals.push_back(&window);
        }
    }

    open_mirrors(std::move(arrivals), listed);
    for (const GuestWindowInfo& window : windows) {
        if (auto mirror = mirrors_.value(window.handle); !mirror.isNull()) {
            follow(*mirror, window, listed);
        }
    }
    close_departed(listed);
    finish_if_done();
}

// Done when there is nothing to show and nothing left to show anything:
// a launcher stub's dialog closing before the program it started has a
// window is neither.
void MirrorSession::finish_if_done() {
    if (application_alive_ || !mirrors_.isEmpty()) return;
    RWIN_INFO("mirror", "no windows and no process; session is done");
    Q_EMIT finished();
}

// An owner is opened before what it owns, whatever order the list came
// in, so an owned window has the surface it is placed against from the
// start. One whose owner is not on the list opens on its own, as a
// toplevel; so does one whose owner chain loops back on itself, which
// Windows does not produce but a wire can.
void MirrorSession::open_mirrors(QList<const GuestWindowInfo*> arrivals,
                                 const Listed& listed) {
    QList<const GuestWindowInfo*> opened;
    opened.reserve(arrivals.size());
    while (!arrivals.isEmpty()) {
        const qsizetype before = arrivals.size();
        for (auto it = arrivals.begin(); it != arrivals.end();) {
            const GuestWindowInfo& window = **it;
            const bool owner_listed = window.owner != 0 && listed.contains(window.owner);
            MirrorWindow* owner = mirrors_.value(window.owner).data();
            if (owner_listed && owner == nullptr) {
                ++it;
                continue;
            }
            open(window, owner);
            opened.push_back(&window);
            it = arrivals.erase(it);
        }
        if (arrivals.size() == before) {
            open(*arrivals.first(), nullptr);
            opened.push_back(arrivals.takeFirst());
        }
    }

    for (const GuestWindowInfo* window : opened) {
        auto mirror = mirrors_.value(window->handle);
        if (mirror.isNull()) continue;
        place(*mirror, *window, listed);
        mirror->show();
        link_.subscribe(window->handle);
    }
}

void MirrorSession::open(const GuestWindowInfo& window, MirrorWindow* owner) {
    RWIN_INFO("mirror", "new window {:#x} owner={:#x}{} '{}' {}x{} class='{}'",
              window.handle, window.owner, owner == nullptr ? "" : " (mirrored)",
              window.title.toStdString(), window.width, window.height,
              window.class_name.toStdString());
    mirrors_.insert(window.handle, new MirrorWindow(link_, window, owner));
}

void MirrorSession::follow(MirrorWindow& mirror, const GuestWindowInfo& window,
                           const Listed& listed) {
    mirror.retitle(window.title);
    mirror.follow_guest_style(window);
    mirror.follow_guest_state(window.minimized, window.maximized);
    mirror.follow_guest_foreground(window.foreground);
    mirror.note_guest_size(window.width, window.height);
    place(mirror, window, listed);
}

// An owner we are not mirroring is no owner at all: the window stands on
// its own rather than being placed against nothing.
void MirrorSession::place(MirrorWindow& mirror, const GuestWindowInfo& window,
                          const Listed& listed) {
    if (window.owner == 0) return;
    auto owner = mirrors_.value(window.owner);
    const GuestWindowInfo* owner_window = listed.value(window.owner);
    if (owner.isNull() || owner_window == nullptr) return;
    mirror.place_under(*owner, QPoint(window.x - owner_window->x, window.y - owner_window->y));
}

void MirrorSession::close_departed(const Listed& listed) {
    for (auto it = mirrors_.begin(); it != mirrors_.end();) {
        if (listed.contains(it.key()) && !it.value().isNull()) {
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
}

void MirrorSession::hand_over_drag(quint64 window) {
    if (auto it = mirrors_.find(window); it != mirrors_.end() && !it.value().isNull()) {
        it.value()->take_over_drag();
    }
}

void MirrorSession::show_cursor(quint64 id) {
    const QCursor* shape = cursor_.find(id);
    if (shape == nullptr) {
        RWIN_DEBUG("cursor", "shape {:#x} named before it arrived", id);
        return;
    }
    // The guest has one cursor at a time and does not say whose window it
    // is over. Every mirror wears it; only the one under the pointer is
    // showing a cursor at all.
    for (auto& mirror : mirrors_) {
        if (!mirror.isNull()) mirror->wear_guest_cursor(*shape);
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
    finish_if_done();
}

}  // namespace rwin::host
