#include "host/mirror/mirror_session.hpp"

#include "host/chrome/win32_style.hpp"
#include "host/export/shared_frame.hpp"
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_window.hpp"
#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/guest_cursor.hpp"

#include <QImage>
#include <QPoint>
#include <QTimer>
#include <algorithm>
#include <utility>
#include <vector>

namespace rwin::host {

namespace {
// The guest pushes the window set whenever it changes, at frame rate, so
// this is only a safety net: it re-asks in case a push was lost with a
// connection, and costs one small message a second.
constexpr int kPollIntervalMs = 1000;
}  // namespace

MirrorSession::MirrorSession(GuestLink& link, SharedFrameFeed& frames, std::uint32_t process_id,
                             QString job_name, QObject* parent)
    : QObject(parent), link_(link), frames_(frames), process_id_(process_id),
      job_name_(std::move(job_name)), poll_(new QTimer(this)), grabber_(link, pointer_globals_),
      placement_(link) {
    connect(&link_, &GuestLink::windows_listed, this, &MirrorSession::refresh);
    connect(&link_, &GuestLink::frame_arrived, this, &MirrorSession::show_frame);
    connect(&frames_, &SharedFrameFeed::frame_ready, this, &MirrorSession::show_shared_frame);
    connect(&link_, &GuestLink::window_gone, this, &MirrorSession::forget);
    connect(&link_, &GuestLink::drag_started, this, &MirrorSession::hand_over_drag);
    connect(&link_, &GuestLink::cursor_changed, &cursor_, &CursorPlayer::show);
    connect(&link_, &GuestLink::cursor_pictured, &cursor_, &CursorPlayer::remember);
    connect(&link_, &GuestLink::pointer_grabbed, this, &MirrorSession::follow_grab);
    connect(&cursor_, &CursorPlayer::worn, this, &MirrorSession::wear_cursor);

    poll_->setInterval(kPollIntervalMs);
    connect(poll_, &QTimer::timeout, this, [this] { link_.watch_scope(process_id_, job_name_); });
    poll_->start();
    connect(&link_, &GuestLink::connected, this,
            [this] { link_.watch_scope(process_id_, job_name_); });
    link_.watch_scope(process_id_, job_name_);
}

MirrorSession::~MirrorSession() {
    // Destroy locally, not close(): a parent ending is not a user's WM_CLOSE.
    for (const auto& mirror : mirrors_) {
        if (mirror)
            delete mirror.data();
    }
    for (const auto& mirror : waiting_release_) {
        if (mirror)
            delete mirror.data();
    }
}

void MirrorSession::raise() {
    for (const GuestWindowInfo& window : listed_) {
        if (Win32Style{window}.disabled() || !Win32Style{window}.activates())
            continue;
        if (auto mirror = mirrors_.value(window.handle); !mirror.isNull()) {
            if (mirror->isMinimized())
                mirror->showNormal();
            link_.send_focus(window.handle);
            mirror->come_forward();
            return;
        }
    }
}

MirrorWindow* MirrorSession::find(std::uint64_t window) const {
    return mirrors_.value(window).data();
}

std::optional<MirrorState> MirrorSession::state_of(std::uint64_t window) const {
    const MirrorWindow* mirror = find(window);
    if (mirror == nullptr)
        return std::nullopt;
    MirrorState state = mirror->state();
    placement_.describe(window, state);
    grabber_.describe(window, state);
    if (state.cursor_shape == Qt::BitmapCursor)
        state.cursor_surface_size = pixel_cursor_.surface_size();
    if (cursor_.shown().shown && state.cursor_id == cursor_.shown().id)
        state.cursor_steps_taken = cursor_.steps_taken();
    return state;
}

void MirrorSession::refresh(const QList<GuestWindowInfo>& windows, bool application_alive,
                            QSize guest_room) {
    application_alive_ = application_alive;
    listed_ = windows;
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
            mirror->limit_to(guest_room);
            follow(*mirror, window, listed);
        }
    }
    close_departed(listed);
    close_let_go();
    // The window may have moved, which moves the clip within it.
    follow_grab(grab_);
    finish_if_done();
}

// Done when there is nothing to show and nothing left to show anything:
// a launcher stub's dialog closing before the program it started has a
// window is neither.
void MirrorSession::finish_if_done() {
    if (application_alive_ || !mirrors_.isEmpty())
        return;
    RWIN_INFO("mirror", "no windows and no process; session is done");
    Q_EMIT finished();
}

// An owner is opened before what it owns, whatever order the list came
// in, so an owned window has the surface it is placed against from the
// start. One whose owner is not on the list opens on its own, as a
// toplevel; so does one whose owner chain loops back on itself, which
// Windows does not produce but a wire can.
void MirrorSession::open_mirrors(QList<const GuestWindowInfo*> arrivals, const Listed& listed) {
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
        if (mirror.isNull())
            continue;
        place(*mirror, *window, listed);
        // In the guest's state from the start: shown normal first, a
        // maximised window's mirror was sized as a normal one and asked the
        // guest for that size before it was told to maximise.
        mirror->follow_guest_state(window->minimized, window->maximized);
        mirror->show();
        link_.subscribe(window->handle);
    }
}

void MirrorSession::open(const GuestWindowInfo& window, MirrorWindow* owner) {
    RWIN_INFO("mirror", "new window {:#x} owner={:#x}{} '{}' {}x{} class='{}'", window.handle,
              window.owner, owner == nullptr ? "" : " (mirrored)", window.title.toStdString(),
              window.width, window.height, window.class_name.toStdString());
    // A window that stands alone is made a toplevel from the start, and
    // attached to its owner when it is placed.
    const bool popup = owner != nullptr && !Win32Style{window}.stands_alone();
    auto* mirror = new MirrorWindow(link_, window, popup ? owner : nullptr,
                                    placement_.tag_for(window.handle), pixel_cursor_);
    // A toplevel is made ready for frames on the GPU before it is first
    // shown; a popup - a menu, mostly, whose frames come as pixels - only
    // once one arrives, rather than an OpenGL window for every menu opened.
    if (frames_.offered()) {
        if (!popup)
            mirror->take_gpu_frames();
        connect(mirror, &MirrorWindow::gpu_failed, this,
                [this](const QString& why) { frames_.give_up(why.toStdString()); });
    }
    // Whatever the guest shows now, not the arrow until it next changes.
    if (cursor_.shown().id != 0 || !cursor_.shown().shown)
        mirror->wear_guest_cursor(cursor_.look(), cursor_.step());
    connect(mirror, &MirrorWindow::pressed_while_disabled, this, &MirrorSession::bring_forward_for);
    connect(mirror, &MirrorWindow::focused_unasked, this, &MirrorSession::restore_guest_activation);
    mirrors_.insert(window.handle, mirror);
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
    auto owner = window.owner == 0 ? QPointer<MirrorWindow>() : mirrors_.value(window.owner);
    const GuestWindowInfo* owner_window = listed.value(window.owner);
    const bool popup =
        !owner.isNull() && owner_window != nullptr && !Win32Style{window}.stands_alone();
    if (popup) {
        mirror.place_under(*owner, QPoint(window.x - owner_window->x, window.y - owner_window->y));
    } else {
        mirror.attach_to(owner_window == nullptr ? nullptr : owner.data());
    }
    // A toplevel goes where the guest put it; a popup is placed against its
    // owner already, and a minimised or maximised one by its state.
    placement_.follow_guest(
        mirror,
        QRect(window.x, window.y, static_cast<int>(window.width), static_cast<int>(window.height)),
        !popup && !window.minimized && !window.maximized);
    if (!popup && window.maximized)
        placement_.keep_on_guest_screen(mirror);
}

void MirrorSession::rehome_popups_of(MirrorWindow& owner, const Listed& listed) {
    for (MirrorWindow* popup : owner.findChildren<MirrorWindow*>(Qt::FindDirectChildrenOnly)) {
        if (listed.contains(popup->handle()))
            popup->attach_to(nullptr);
    }
}

// Windows activates "the enabled popup window owned by" the window clicked
// (GW_ENABLEDPOPUP): an enabled window it owns, or owns through windows it
// owns, the topmost of them - a modal dialog's own modal dialog when there
// is one. The list is in the guest's z-order, topmost first.
void MirrorSession::bring_forward_for(std::uint64_t window) {
    for (const GuestWindowInfo& candidate : listed_) {
        if (Win32Style{candidate}.disabled() || !owned_by(candidate, window))
            continue;
        if (auto mirror = mirrors_.value(candidate.handle); !mirror.isNull()) {
            mirror->come_forward();
            return;
        }
    }
    RWIN_DEBUG("mirror", "{:#x} pressed while disabled, and owns nothing enabled", window);
}

void MirrorSession::restore_guest_activation() {
    const auto foreground =
        std::find_if(listed_.begin(), listed_.end(),
                     [](const GuestWindowInfo& window) { return window.foreground; });
    if (foreground == listed_.end())
        return;
    if (auto mirror = mirrors_.value(foreground->handle); !mirror.isNull())
        mirror->reclaim_activation();
}

bool MirrorSession::owned_by(const GuestWindowInfo& window, std::uint64_t owner) const {
    std::uint64_t up = window.owner;
    // A chain no longer than the list: one that loops is no chain at all.
    for (qsizetype steps = 0; up != 0 && steps < listed_.size(); ++steps) {
        if (up == owner)
            return true;
        const auto next =
            std::find_if(listed_.begin(), listed_.end(),
                         [up](const GuestWindowInfo& listed) { return listed.handle == up; });
        if (next == listed_.end())
            return false;
        up = next->owner;
    }
    return false;
}

void MirrorSession::close_departed(const Listed& listed) {
    for (auto it = mirrors_.begin(); it != mirrors_.end();) {
        if (listed.contains(it.key()) && !it.value().isNull()) {
            ++it;
            continue;
        }
        RWIN_INFO("mirror", "window {:#x} left the list; closing its mirror", it.key());
        if (auto mirror = it.value(); !mirror.isNull()) {
            // Photoshop hides a panel the moment it is picked up. The
            // surface the press landed on has to outlive the panel, or
            // the compositor delivers the rest of the drag to nobody.
            rehome_popups_of(*mirror, listed);
            if (mirror->holds_press()) {
                mirror->close_when_released();
                connect(mirror, &MirrorWindow::let_go, this, &MirrorSession::close_let_go);
                waiting_release_.push_back(mirror);
            } else {
                mirror->guest_closed();
            }
        }
        link_.unsubscribe(it.key());
        placement_.forget(it.key());
        it = mirrors_.erase(it);
    }
}

// A mirror kept for a press closes once nothing in it holds one — its
// own release, or the release of a popup of its that it had to outlive.
void MirrorSession::close_let_go() {
    for (auto it = waiting_release_.begin(); it != waiting_release_.end();) {
        if (it->isNull()) {
            it = waiting_release_.erase(it);
            continue;
        }
        if ((*it)->holds_press()) {
            ++it;
            continue;
        }
        (*it)->guest_closed();
        it = waiting_release_.erase(it);
    }
}

void MirrorSession::hand_over_drag(quint64 window) {
    if (auto it = mirrors_.find(window); it != mirrors_.end() && !it.value().isNull()) {
        it.value()->take_over_drag();
    }
}

void MirrorSession::wear_cursor(const CursorLook* look, std::size_t step) {
    for (auto& mirror : mirrors_) {
        if (!mirror.isNull())
            mirror->wear_guest_cursor(look, step);
    }
}

void MirrorSession::follow_grab(const PointerGrab& grab) {
    grab_ = grab;
    MirrorWindow* mirror =
        grab.kind == PointerGrab::Kind::None ? nullptr : mirrors_.value(grab.window).data();
    const auto listed =
        std::find_if(listed_.begin(), listed_.end(),
                     [&](const GuestWindowInfo& window) { return window.handle == grab.window; });
    const QPoint origin = listed == listed_.end() ? QPoint() : QPoint(listed->x, listed->y);
    grabber_.follow(grab, mirror, origin);
}

void MirrorSession::show_frame(quint64 window, const QImage& pixels) {
    if (auto it = mirrors_.find(window); it != mirrors_.end() && !it.value().isNull()) {
        it.value()->present(pixels);
        if (!ready_) {
            ready_ = true;
            Q_EMIT ready();
        }
    }
}

void MirrorSession::show_shared_frame(quint64 window, const SharedFrame& frame) {
    if (auto it = mirrors_.find(window); it != mirrors_.end() && !it.value().isNull()) {
        it.value()->present_shared(frame);
        if (!ready_) {
            ready_ = true;
            Q_EMIT ready();
        }
    }
}

void MirrorSession::forget(quint64 window) {
    auto it = mirrors_.find(window);
    if (it == mirrors_.end())
        return;
    if (!it.value().isNull())
        it.value()->guest_closed();
    mirrors_.erase(it);
    placement_.forget(window);
    finish_if_done();
}

}  // namespace rwin::host
