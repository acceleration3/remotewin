#include "host/mirror/mirror_session.hpp"

#include "host/chrome/win32_style.hpp"
#include "host/export/shared_frame.hpp"
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "host/mirror/mirror_window.hpp"
#include "host/wayland/relative_pointer.hpp"
#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/sys_command.hpp"
#include "proto/sys_param.hpp"
#include "proto/window_fields.hpp"

#include <QImage>
#include <QPoint>
#include <QTimer>

#include <unordered_set>
#include <utility>

namespace rwin::host {

namespace {

// How long an application with no windows is waited on before it is done:
// a launcher stub giving way to the program it started, an application
// between closing one window and opening the next - Steam's updater giving
// way to its sign-in - has none for a moment, and is not done.
constexpr int kEmptyGraceMs = 5000;

// What a change to these fields can change: whose a window is, and whether
// it is shown.
constexpr std::uint32_t kBelongingFields = WindowFields::kOwner | WindowFields::kAppId | WindowFields::kPopup
                                           | WindowFields::kShow | WindowFields::kStyle;

}  // namespace

MirrorSession::MirrorSession(GuestLink& link, SharedFrameFeed& frames, MirrorScope scope, QObject* parent)
    : QObject(parent), link_(link), frames_(frames), scope_(std::move(scope)), grabber_(link, pointer_globals_),
      placement_(link), activation_(link), empty_(new QTimer(this)) {
    connect(&link_, &GuestLink::connected, this, &MirrorSession::connected);
    connect(&link_, &GuestLink::sync_began, this, &MirrorSession::sync_began);
    connect(&link_, &GuestLink::sync_ended, this, &MirrorSession::sync_ended);
    connect(&link_, &GuestLink::window_new, this, &MirrorSession::window_new);
    connect(&link_, &GuestLink::window_changed, this, &MirrorSession::window_changed);
    connect(&link_, &GuestLink::window_gone, this, &MirrorSession::window_gone);
    connect(&link_, &GuestLink::window_limited, this, &MirrorSession::window_limited);
    connect(&link_, &GuestLink::work_area_changed, this, &MirrorSession::work_area_changed);
    connect(&link_, &GuestLink::restacked, this, &MirrorSession::restacked);
    connect(&link_, &GuestLink::move_size_started, this, &MirrorSession::move_size_started);
    connect(&link_, &GuestLink::move_size_ended, this, [](const MoveSizeEnd& end) {
        RWIN_DEBUG("mirror", "Windows' move or size of {} ended at {},{} {}x{}", end.window, end.frame.x,
                   end.frame.y, end.frame.width, end.frame.height);
    });
    connect(&link_, &GuestLink::frame_arrived, this, &MirrorSession::show_frame);
    connect(&frames_, &SharedFrameFeed::frame_ready, this, &MirrorSession::show_shared_frame);
    connect(&link_, &GuestLink::cursor_changed, &cursor_, &CursorPlayer::show);
    connect(&link_, &GuestLink::cursor_pictured, &cursor_, &CursorPlayer::remember);
    connect(&link_, &GuestLink::pointer_grabbed, this, &MirrorSession::follow_grab);
    connect(&cursor_, &CursorPlayer::worn, this, &MirrorSession::wear_cursor);
    empty_->setSingleShot(true);
    empty_->setInterval(kEmptyGraceMs);
    connect(empty_, &QTimer::timeout, this, [this] {
        RWIN_INFO("mirror", "the application has had no window for {} ms; the session is done", kEmptyGraceMs);
        Q_EMIT finished();
    });
    if (pointer_globals_.relative() != nullptr && PointerGlobals::pointer() != nullptr) {
        held_motion_ = std::make_unique<RelativePointer>(pointer_globals_.relative(), PointerGlobals::pointer());
        connect(held_motion_.get(), &RelativePointer::moved_by, this, [this](QPointF logical) {
            for (const auto& mirror : std::as_const(mirrors_)) {
                if (!mirror.isNull() && mirror->holds()) mirror->held_motion(logical);
            }
        });
    } else {
        RWIN_INFO("pointer", "no relative pointer: a window Windows drags is followed against its mirror");
    }
}

MirrorSession::~MirrorSession() {
    // Destroy locally, not close(): a parent ending is not a user's close.
    for (const auto& mirror : std::as_const(mirrors_)) {
        if (mirror) delete mirror.data();
    }
    for (const auto& mirror : std::as_const(waiting_release_)) {
        if (mirror) delete mirror.data();
    }
}

const GuestWindows& MirrorSession::windows() const noexcept { return link_.windows(); }

bool MirrorSession::belongs(WindowId window) const { return scope_.belongs(window, windows()); }

MirrorWindow* MirrorSession::find(WindowId window) const { return mirrors_.value(window.key()).data(); }

std::optional<MirrorState> MirrorSession::state_of(WindowId window) const {
    const MirrorWindow* mirror = find(window);
    if (mirror == nullptr) return std::nullopt;
    MirrorState state = mirror->state();
    placement_.describe(window, state);
    grabber_.describe(window, state);
    if (state.cursor_shape == Qt::BitmapCursor) state.cursor_surface_size = pixel_cursor_.surface_size();
    if (cursor_.shown().shown && state.cursor_id == cursor_.shown().id) state.cursor_steps_taken = cursor_.steps_taken();
    return state;
}

// Windows moves a window it drags only at the end of the drag without
// this, drawing an outline on a desktop nobody sees.
void MirrorSession::connected() {
    link_.send_sys_param(SysParam{SysParam::Param::DragFullWindows, {}, true});
}

void MirrorSession::sync_began() { syncing_ = true; }

void MirrorSession::sync_ended() {
    syncing_ = false;
    reconcile();
}

void MirrorSession::window_new() { reconcile(); }

void MirrorSession::window_changed(WindowId window, quint32 mask) {
    if ((mask & kBelongingFields) != 0) reconcile();
    MirrorWindow* mirror = find(window);
    const GuestWindow* known = windows().find(window);
    if (mirror != nullptr && known != nullptr) follow(*mirror, *known, mask);
    if ((mask & WindowFields::kGeometry) != 0) place_popups_of(window);
    // The window may have moved, which moves the clip within it.
    if (grab_.window == window) follow_grab(grab_);
}

void MirrorSession::window_gone(WindowId window) {
    close_mirror(window);
    reconcile();
}

void MirrorSession::window_limited(WindowId window) {
    MirrorWindow* mirror = find(window);
    const GuestWindow* known = windows().find(window);
    if (mirror != nullptr && known != nullptr) mirror->follow_guest_limits(known->limits());
}

void MirrorSession::work_area_changed(QRect area) {
    work_area_ = area.size();
    for (const auto& mirror : std::as_const(mirrors_)) {
        if (!mirror.isNull()) mirror->limit_to(work_area_);
    }
}

void MirrorSession::restacked(const ZOrder& order) {
    for (const auto& mirror : std::as_const(mirrors_)) {
        if (!mirror.isNull()) mirror->note_guest_active(mirror->window() == order.active);
    }
    activation_.follow(order, find(order.active));
}

void MirrorSession::move_size_started(const MoveSizeStart& start) {
    if (MirrorWindow* mirror = find(start.window)) mirror->take_over_move_size(start.kind);
}

void MirrorSession::reconcile() {
    if (syncing_) return;
    claim_applications();
    std::vector<const GuestWindow*> arrivals;
    std::size_t belonging = 0;
    for (const GuestWindow* window : windows().stacked()) {
        if (!belongs(window->id())) continue;
        ++belonging;
        if (!window->hidden() && find(window->id()) == nullptr) arrivals.push_back(window);
    }
    std::vector<WindowId> strays;
    for (auto it = mirrors_.cbegin(); it != mirrors_.cend(); ++it) {
        if (!belongs(WindowId::of_key(it.key()))) strays.push_back(WindowId::of_key(it.key()));
    }
    for (const WindowId stray : strays) {
        RWIN_INFO("mirror", "{} is no longer the application's; closing its mirror", stray);
        close_mirror(stray);
        link_.unsubscribe(stray);
    }
    open_mirrors(std::move(arrivals));
    close_let_go();
    count_windows(belonging);
}

void MirrorSession::claim_applications() {
    for (const std::string& aumid : scope_.claim(windows())) {
        RWIN_INFO("mirror", "the application is known as '{}'", aumid);
        Q_EMIT app_claimed(QString::fromStdString(aumid));
    }
}

// Done once it has had a window and has had none for a while.
void MirrorSession::count_windows(std::size_t belonging) {
    if (belonging != 0) {
        had_windows_ = true;
        empty_->stop();
        return;
    }
    if (had_windows_ && !empty_->isActive()) empty_->start();
}

// A window a popup is placed against is opened before the popup, whatever
// order they came in, so the popup has the surface it is placed against
// from the start. One whose parent is not coming opens on its own, as a
// toplevel; so does one whose chain loops back on itself, which Windows
// does not produce but a wire can.
void MirrorSession::open_mirrors(std::vector<const GuestWindow*> arrivals) {
    std::vector<const GuestWindow*> opened;
    opened.reserve(arrivals.size());
    std::unordered_set<WindowId> waiting;
    for (const GuestWindow* window : arrivals) waiting.insert(window->id());
    while (!arrivals.empty()) {
        const std::size_t before = arrivals.size();
        for (auto it = arrivals.begin(); it != arrivals.end();) {
            const WindowId parent = popup_parent_of(**it);
            if (parent != (*it)->id() && waiting.contains(parent)) {
                ++it;
                continue;
            }
            open(**it);
            waiting.erase((*it)->id());
            opened.push_back(*it);
            it = arrivals.erase(it);
        }
        if (arrivals.size() == before) {
            open(*arrivals.front());
            waiting.erase(arrivals.front()->id());
            opened.push_back(arrivals.front());
            arrivals.erase(arrivals.begin());
        }
    }
    for (const GuestWindow* window : opened) {
        MirrorWindow* mirror = find(window->id());
        if (mirror == nullptr) continue;
        place(*mirror, *window);
        // In the guest's state from the start: shown normal first, a
        // maximised window's mirror was sized as a normal one and asked the
        // guest for that size before it was told to maximise.
        mirror->follow_guest_state(window->minimized(), window->maximized());
        mirror->show();
        link_.subscribe(window->id());
    }
}

void MirrorSession::open(const GuestWindow& window) {
    MirrorWindow* parent = find(popup_parent_of(window));
    RWIN_INFO("mirror", "new window {} owner {}{} '{}' {}x{} class '{}' pid {} app '{}'", window.id(),
              window.fields().owner, parent == nullptr ? "" : " (a popup)", window.fields().title,
              window.picture_size().width(), window.picture_size().height(), window.fields().class_name,
              window.fields().app_id.process_id, window.fields().app_id.aumid);
    auto* mirror = new MirrorWindow(link_, window, parent, placement_.tag_for(window.id()), pixel_cursor_);
    // A toplevel is made ready for frames on the GPU before it is first
    // shown; a popup - a menu, mostly, whose frames come as pixels - only
    // once one arrives, rather than a picture window for every menu opened.
    if (frames_.offered() && parent == nullptr) mirror->take_gpu_frames();
    mirror->note_guest_active(windows().order().active == window.id());
    if (work_area_.isValid()) mirror->limit_to(work_area_);
    // Whatever the guest shows now, not the arrow until it next changes.
    if (cursor_.shown().id != 0 || !cursor_.shown().shown) mirror->wear_guest_cursor(cursor_.look(), cursor_.step());
    mirror->follow_relative_motion(held_motion_ != nullptr);
    watch(*mirror);
    mirrors_.insert(window.id().key(), mirror);
}

void MirrorSession::watch(MirrorWindow& mirror) {
    if (frames_.offered()) {
        const WindowId window = mirror.window();
        connect(&mirror, &MirrorWindow::gpu_failed, this,
                [this, window](const QString& why) { frames_.pixels_for(window, why.toStdString()); });
        connect(&mirror, &MirrorWindow::frame_timed, this,
                [this](const FrameStamps& stamps) { frames_.shown(stamps); });
    }
    connect(&mirror, &MirrorWindow::pressed_while_disabled, this, &MirrorSession::bring_forward_for);
    connect(&mirror, &MirrorWindow::focused, &activation_, &ActivationSync::focused);
    // Placed again where the guest has it: deltas carry nothing new after a
    // restyle, and the compositor's own placement of the new surface would
    // otherwise be taken for the user moving it.
    connect(&mirror, &MirrorWindow::surface_made, this, [this](WindowId window) {
        MirrorWindow* remade = find(window);
        const GuestWindow* known = windows().find(window);
        if (remade != nullptr && known != nullptr && !known->hidden()) place(*remade, *known);
    });
}

void MirrorSession::follow(MirrorWindow& mirror, const GuestWindow& window, quint32 mask) {
    mirror.retitle(window.title());
    mirror.follow_guest_style(window);
    if (window.hidden()) {
        if (!mirror.isHidden()) mirror.hide();
        return;
    }
    if ((mask & WindowFields::kGeometry) != 0) mirror.follow_guest_geometry(window);
    mirror.follow_guest_state(window.minimized(), window.maximized());
    place(mirror, window);
    if (mirror.isHidden()) mirror.show();
}

// A parent that has no mirror is no parent at all: the window stands on
// its own rather than being placed against nothing.
void MirrorSession::place(MirrorWindow& mirror, const GuestWindow& window) {
    const WindowId parent_id = popup_parent_of(window);
    const GuestWindow* parent = parent_id == window.id() ? nullptr : windows().find(parent_id);
    MirrorWindow* parent_mirror = parent == nullptr ? nullptr : find(parent_id);
    const bool popup = parent_mirror != nullptr;
    if (popup) {
        const QPoint offset = window.owner() == parent_id ? window.picture_origin() - parent->picture_origin()
                                                          : window.offset_from_anchor(*parent);
        mirror.place_under(*parent_mirror, offset);
    } else {
        mirror.attach_to(find(window.owner()));
    }
    // A toplevel goes where the guest put it; a popup is placed against its
    // parent already, and a minimised or maximised one by its state.
    placement_.follow_guest(mirror, window, !popup && !window.minimized() && !window.maximized());
    if (!popup && window.maximized()) placement_.keep_on_guest_screen(mirror);
}

WindowId MirrorSession::popup_parent_of(const GuestWindow& window) const {
    if (!window.owner().none()) return Win32Style{window}.stands_alone() ? WindowId{} : window.owner();
    return window.anchor();
}

void MirrorSession::place_popups_of(WindowId window) {
    for (const auto& mirror : std::as_const(mirrors_)) {
        if (mirror.isNull()) continue;
        const GuestWindow* known = windows().find(mirror->window());
        if (known != nullptr && popup_parent_of(*known) == window) place(*mirror, *known);
    }
}

void MirrorSession::close_mirror(WindowId window) {
    const auto it = mirrors_.find(window.key());
    if (it == mirrors_.end()) return;
    const QPointer<MirrorWindow> mirror = it.value();
    mirrors_.erase(it);
    placement_.forget(window);
    if (mirror.isNull()) return;
    rehome_popups_of(*mirror);
    // Photoshop hides a panel the moment it is picked up. The surface the
    // press landed on has to outlive the panel, or the compositor delivers
    // the rest of the drag to nobody.
    if (mirror->holds_press()) {
        mirror->close_when_released();
        connect(mirror, &MirrorWindow::let_go, this, &MirrorSession::close_let_go);
        waiting_release_.push_back(mirror);
    } else {
        mirror->guest_closed();
    }
}

void MirrorSession::rehome_popups_of(MirrorWindow& owner) {
    for (MirrorWindow* popup : owner.findChildren<MirrorWindow*>(Qt::FindDirectChildrenOnly)) {
        if (find(popup->window()) == popup) popup->attach_to(nullptr);
    }
}

// A mirror kept for a press closes once nothing in it holds one — its own
// release, or the release of a popup of its that it had to outlive.
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

void MirrorSession::raise() {
    for (const GuestWindow* window : windows().stacked()) {
        const Win32Style style{*window};
        if (style.disabled() || !style.activates() || !belongs(window->id())) continue;
        MirrorWindow* mirror = find(window->id());
        if (mirror == nullptr || mirror->parentWidget() != nullptr) continue;
        if (window->minimized()) {
            link_.send_sys_command(window->id(), SysCommand::Command::Restore);
            mirror->unminimize();
        }
        link_.send_activate(window->id(), true);
        mirror->raise();
        mirror->activate_for_guest();
        return;
    }
}

// Windows activates "the enabled popup window owned by" the window clicked
// (GW_ENABLEDPOPUP): an enabled window it owns, or owns through windows it
// owns, the topmost of them - a modal dialog's own modal dialog when there
// is one.
void MirrorSession::bring_forward_for(WindowId window) {
    for (const GuestWindow* candidate : windows().stacked()) {
        if (Win32Style{*candidate}.disabled() || !owned_by(*candidate, window)) continue;
        if (MirrorWindow* mirror = find(candidate->id())) {
            mirror->come_forward();
            return;
        }
    }
    RWIN_DEBUG("mirror", "{} pressed while disabled, and owns nothing enabled", window);
}

bool MirrorSession::owned_by(const GuestWindow& window, WindowId owner) const {
    WindowId up = window.owner();
    // A chain no longer than there are windows: one that loops is no chain.
    for (std::size_t steps = 0; !up.none() && steps < windows().size(); ++steps) {
        if (up == owner) return true;
        const GuestWindow* next = windows().find(up);
        if (next == nullptr) return false;
        up = next->owner();
    }
    return false;
}

void MirrorSession::wear_cursor(const CursorLook* look, std::size_t step) {
    for (const auto& mirror : std::as_const(mirrors_)) {
        if (!mirror.isNull()) mirror->wear_guest_cursor(look, step);
    }
}

void MirrorSession::follow_grab(const PointerGrab& grab) {
    grab_ = grab;
    MirrorWindow* mirror = grab.kind == PointerGrab::Kind::None ? nullptr : find(grab.window);
    const GuestWindow* window = windows().find(grab.window);
    grabber_.follow(grab, mirror, window == nullptr ? QPoint() : window->picture_origin());
}

void MirrorSession::show_frame(WindowId window, const QImage& pixels) {
    MirrorWindow* mirror = find(window);
    if (mirror == nullptr) return;
    mirror->present(pixels);
    note_ready();
}

void MirrorSession::show_shared_frame(WindowId window, const SharedFrame& frame) {
    MirrorWindow* mirror = find(window);
    if (mirror == nullptr) return;
    mirror->present_shared(frame);
    note_ready();
}

void MirrorSession::note_ready() {
    if (ready_) return;
    ready_ = true;
    Q_EMIT ready();
}

}  // namespace rwin::host
