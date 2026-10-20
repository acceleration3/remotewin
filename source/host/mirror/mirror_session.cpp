#include "host/mirror/mirror_session.hpp"

#include "host/chrome/win32_style.hpp"
#include "host/export/shared_frame.hpp"
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "host/wayland/globals.hpp"
#include "host/wayland/seat.hpp"
#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/move_size_end.hpp"
#include "proto/move_size_start.hpp"
#include "proto/sys_command.hpp"
#include "proto/sys_param.hpp"
#include "proto/window_fields.hpp"

#include <algorithm>
#include <utility>

namespace rwin::host {

namespace {

// How long an application with no windows is waited on before it is done:
// a launcher stub giving way to the program it started, an application
// between closing one window and opening the next - Steam's updater giving
// way to its sign-in - has none for a moment, and is not done.
constexpr std::chrono::milliseconds kEmptyGrace{5000};

// What a change to these fields can change: whose a window is, whether it
// is shown, and what it is to this desktop.
constexpr std::uint32_t kBelongingFields = WindowFields::kOwner | WindowFields::kAppId | WindowFields::kPopup
                                           | WindowFields::kShow | WindowFields::kStyle | WindowFields::kZGlue;

}  // namespace

MirrorSession::MirrorSession(const Parts& parts, MirrorScope scope, std::string app_id, std::string token)
    : loop_(parts.loop), link_(parts.link), frames_(parts.frames), scope_(std::move(scope)),
      activation_(parts.surfaces.globals, parts.surfaces.seat),
      grabber_(parts.link, parts.surfaces.globals, parts.surfaces.seat),
      placement_(parts.loop, parts.bus),
      kit_{parts.link, parts.surfaces, activation_, popups_, grabber_, desktop_, placement_, std::move(app_id)},
      cursor_(parts.loop), focus_(parts.loop, parts.link, parts.surfaces.seat), token_(std::move(token)),
      empty_(parts.loop.timer([this] {
          RWIN_INFO("mirror", "the application has had no window for {} ms; the session is done", kEmptyGrace.count());
          finished();
      })),
      rearrange_(parts.loop.timer([this] { arrange(); })) {
    // Windows moves a window it drags only at the end of the drag without
    // this, drawing an outline on a desktop nobody sees.
    link_.connected.connect([this] { link_.send_sys_param(SysParam{SysParam::Param::DragFullWindows, {}, true}); });
    link_.sync_began.connect([this] { syncing_ = true; });
    link_.sync_ended.connect([this] {
        syncing_ = false;
        reconcile();
    });
    link_.window_new.connect([this](WindowId) { reconcile(); });
    link_.window_changed.connect([this](WindowId window, std::uint32_t mask) { window_changed(window, mask); });
    link_.window_gone.connect([this](WindowId window) { window_gone(window); });
    link_.window_limited.connect([this](WindowId window) {
        WindowMirror* mirror = find(window);
        const GuestWindow* known = windows().find(window);
        if (mirror != nullptr && known != nullptr) mirror->follow(*known);
    });
    link_.work_area_changed.connect([this](const Box& area) {
        desktop_.work_area = area;
        desktop_changed();
    });
    link_.monitor_changed.connect([this](const Box& area) {
        desktop_.monitor = area;
        desktop_changed();
    });
    link_.restacked.connect([this](const ZOrder& order) { restacked(order); });
    link_.move_size_started.connect([this](const MoveSizeStart& start) {
        if (WindowMirror* mirror = find(start.window)) mirror->take_over_move_size(start.kind);
    });
    placement_.moved_here.connect([this](WindowId window, Point picture_corner) {
        if (WindowMirror* mirror = find(window)) mirror->move_guest_to(picture_corner);
    });
    placement_.minimized_here.connect([this](WindowId window, bool minimized) {
        if (WindowMirror* mirror = find(window)) mirror->compositor_minimized(minimized);
    });
    link_.move_size_ended.connect([](const MoveSizeEnd& end) {
        RWIN_DEBUG("mirror", "Windows' move or size of {} ended at {},{} {}x{}", end.window, end.frame.x, end.frame.y,
                   end.frame.width, end.frame.height);
    });
    link_.frame_arrived.connect([this](WindowId window, const PixelsView& pixels) {
        if (WindowMirror* mirror = find(window)) {
            mirror->show_frame(pixels);
            note_ready();
        }
    });
    frames_.frame_ready.connect([this](WindowId window, const SharedFrame& frame) {
        if (WindowMirror* mirror = find(window)) {
            mirror->show_frame(frame);
            note_ready();
        }
    });
    link_.window_icon.connect([this](const WindowIcon& icon) {
        icons_.insert_or_assign(icon.window, icon);
        if (WindowMirror* mirror = find(icon.window)) mirror->set_icon(icon);
    });
    link_.cursor_changed.connect([this](const GuestCursor& cursor) { cursor_.show(cursor); });
    link_.cursor_pictured.connect([this](const CursorPicture& picture) { cursor_.remember(picture); });
    cursor_.worn.connect([this](const CursorLook* look, std::size_t step) {
        for (const auto& [window, mirror] : mirrors_) mirror->wear_cursor(look, step);
    });
    link_.pointer_grabbed.connect([this](const PointerGrab& grab) { follow_grab(grab); });
    parts.surfaces.seat.pointer_changed.connect([this] { follow_pointer(); });
    follow_pointer();
}

MirrorSession::~MirrorSession() {
    // Parts before what they are parts of, popups before what they open
    // from: a parent never goes while something is placed against it.
    while (!mirrors_.empty()) {
        const auto leaf = std::find_if(mirrors_.begin(), mirrors_.end(), [this](const auto& entry) {
            return std::none_of(mirrors_.begin(), mirrors_.end(),
                                [&](const auto& other) { return other.second->parent() == entry.second.get(); });
        });
        mirrors_.erase(leaf != mirrors_.end() ? leaf : mirrors_.begin());
    }
    releasing_.clear();
}

const GuestWindows& MirrorSession::windows() const noexcept { return link_.windows(); }

bool MirrorSession::belongs(WindowId window) const { return scope_.belongs(window, windows()); }

WindowMirror* MirrorSession::find(WindowId window) const {
    const auto found = mirrors_.find(window);
    return found == mirrors_.end() ? nullptr : found->second.get();
}

std::optional<MirrorState> MirrorSession::state_of(WindowId window) const {
    const WindowMirror* mirror = find(window);
    if (mirror == nullptr) return std::nullopt;
    return mirror->state();
}

void MirrorSession::window_changed(WindowId window, std::uint32_t mask) {
    WindowMirror* mirror = find(window);
    const GuestWindow* known = windows().find(window);
    if (mirror != nullptr && known != nullptr) mirror->follow(*known);
    if ((mask & kBelongingFields) != 0) {
        reconcile();
    } else if ((mask & WindowFields::kGeometry) != 0) {
        arrange();
    }
    // The window may have moved, which moves the clip within it.
    if (grab_.window == window) follow_grab(grab_);
}

void MirrorSession::window_gone(WindowId window) {
    icons_.erase(window);
    close(window);
    reconcile();
}

void MirrorSession::desktop_changed() {
    for (const auto& [window, mirror] : mirrors_) {
        if (const GuestWindow* known = windows().find(window)) mirror->follow(*known);
    }
}

void MirrorSession::restacked(const ZOrder& order) {
    for (const auto& [window, mirror] : mirrors_) mirror->note_guest_active(window == order.active);
    focus_.follow(order, find(order.active));
}

void MirrorSession::reconcile() {
    if (syncing_) return;
    claim_applications();
    std::size_t belonging = 0;
    for (const GuestWindow* window : windows().stacked()) {
        if (!belongs(window->id())) continue;
        ++belonging;
        if (find(window->id()) == nullptr && !window->hidden()) open(*window);
    }
    std::vector<WindowId> strays;
    for (const auto& [window, mirror] : mirrors_) {
        if (windows().find(window) == nullptr || !belongs(window)) strays.push_back(window);
    }
    for (const WindowId stray : strays) {
        RWIN_INFO("mirror", "{} is no longer the application's; closing its mirror", stray);
        close(stray);
        link_.unsubscribe(stray);
    }
    arrange();
    count_windows(belonging);
}

void MirrorSession::claim_applications() {
    for (const std::string& aumid : scope_.claim(windows())) {
        RWIN_INFO("mirror", "the application is known as '{}'", aumid);
        app_claimed(aumid);
    }
}

// Done once it has had a window and has had none for a while.
void MirrorSession::count_windows(std::size_t belonging) {
    if (belonging != 0) {
        had_windows_ = true;
        empty_->stop();
        return;
    }
    if (had_windows_ && !empty_->active()) empty_->start(kEmptyGrace, false);
}

WindowMirror& MirrorSession::open(const GuestWindow& window) {
    RWIN_INFO("mirror", "new window {} owner {} anchor {} '{}' {}x{} class '{}' pid {} app '{}'", window.id(),
              window.fields().owner, window.anchor(), window.fields().title, window.picture_size().width,
              window.picture_size().height, window.fields().class_name, window.fields().app_id.process_id,
              window.fields().app_id.aumid);
    auto made = std::make_unique<WindowMirror>(kit_, window);
    WindowMirror& mirror = *made;
    mirror.focused.connect([this](WindowId focused, FocusCause cause) {
        if (focus_.focused(focused, cause) || cause != FocusCause::Mapping) return;
        focus_.follow(windows().order(), find(windows().order().active));
    });
    mirror.pressed_while_disabled.connect([this](WindowId pressed) { bring_forward_for(pressed); });
    mirror.button_pressed.connect([this](WindowId pressed) { placement_.end_drag_hold(pressed); });
    mirror.drag_handed.connect([this](WindowId dragged) { placement_.hold_for_drag(dragged); });
    mirror.resized_by_guest.connect([this](WindowId resized) { placement_.guest_resized(resized); });
    mirror.shared_refused.connect(
        [this](WindowId refused, const std::string& why) { frames_.pixels_for(refused, why); });
    mirror.presented.connect([this](const FrameStamps& stamps) {
        frames_.shown(stamps, kit_.surfaces.seat.keyboard_focus() != nullptr);
    });
    mirror.let_go.connect([this](WindowId) { release_closed(); });
    mirror.remade.connect([this](WindowId) { rearrange_->start(std::chrono::milliseconds::zero(), false); });
    mirror.dropped.connect([this](WindowId) { rearrange_->start(std::chrono::milliseconds::zero(), false); });
    mirror.reshaped.connect([this](WindowId) { rearrange_->start(std::chrono::milliseconds::zero(), false); });
    mirror.mapped_now.connect([this, &mirror](WindowId) {
        rearrange_->start(std::chrono::milliseconds::zero(), false);
        if (!token_.empty() && mirror.role().kind == MirrorRole::Kind::Toplevel) {
            RWIN_INFO("focus", "{} activated with the launcher's token", mirror.window());
            mirror.activate(std::exchange(token_, std::string()));
            return;
        }
        // The guest made it active before it had a surface here: a dialog
        // Windows activates as it opens is followed once it is on screen.
        if (windows().order().active == mirror.window()) focus_.follow(windows().order(), &mirror);
    });
    if (const auto icon = icons_.find(window.id()); icon != icons_.end()) mirror.set_icon(icon->second);
    mirror.note_guest_active(windows().order().active == window.id());
    mirror.follow_relative_motion(held_motion_ != nullptr);
    if (cursor_.shown().id != 0 || !cursor_.shown().shown) mirror.wear_cursor(cursor_.look(), cursor_.step());
    mirrors_.emplace(window.id(), std::move(made));
    link_.subscribe(window.id());
    return mirror;
}

// What is placed against a window going stands on its own until the
// arranging that follows says otherwise.
void MirrorSession::close(WindowId window) {
    const auto found = mirrors_.find(window);
    if (found == mirrors_.end()) return;
    std::unique_ptr<WindowMirror> mirror = std::move(found->second);
    mirrors_.erase(found);
    placement_.forget(window);
    for (const auto& [id, other] : mirrors_) {
        if (other->parent() == mirror.get()) other->orphan();
    }
    for (const auto& other : releasing_) {
        if (other->parent() == mirror.get()) other->orphan();
    }
    // Photoshop hides a panel the moment it is picked up. The surface the
    // press landed on has to outlive the panel, or the compositor delivers
    // the rest of the drag to nobody.
    if (mirror->holds_press()) {
        RWIN_DEBUG("input", "{} gone in the guest but holding a press; staying until release", window);
        releasing_.push_back(std::move(mirror));
    }
}

void MirrorSession::release_closed() {
    std::erase_if(releasing_, [](const std::unique_ptr<WindowMirror>& mirror) { return !mirror->holds_press(); });
}

// What the guest hid, or has not given a size yet, goes before anything is
// shown: a menu bar's next menu
// opens while the last one closes, and a grabbing popup made while another
// chain still grabs is one the compositor refuses (not_the_topmost_popup).
void MirrorSession::arrange() {
    for (const GuestWindow* window : windows().stacked()) {
        if (window->showable()) continue;
        if (WindowMirror* mirror = find(window->id())) mirror->hide();
    }
    std::vector<WindowId> arranging;
    for (const GuestWindow* window : windows().stacked()) {
        if (find(window->id()) == nullptr) continue;
        arranging.clear();
        arrange_one(*window, arranging);
    }
}

// What a window is placed against is arranged first, so a popup always has
// the surface it opens from; a chain that loops back on itself, which
// Windows does not make but a wire can, ends as a toplevel.
void MirrorSession::arrange_one(const GuestWindow& window, std::vector<WindowId>& arranging) {
    WindowMirror* mirror = find(window.id());
    if (mirror == nullptr) return;
    if (!window.showable()) {
        mirror->hide();
        return;
    }
    if (std::find(arranging.begin(), arranging.end(), window.id()) != arranging.end()) return;
    arranging.push_back(window.id());
    MirrorRole role = MirrorRole::of(window, windows());
    WindowMirror* parent = role.parent.none() ? nullptr : find(role.parent);
    const GuestWindow* parent_window = parent != nullptr ? windows().find(role.parent) : nullptr;
    if (parent_window != nullptr && parent_window->showable()
        && std::find(arranging.begin(), arranging.end(), role.parent) == arranging.end()) {
        arrange_one(*parent_window, arranging);
    }
    const bool parent_usable = parent != nullptr && parent_window != nullptr && parent_window->showable()
                               && std::find(arranging.begin(), arranging.end(), role.parent) != arranging.end()
                               && (role.kind != MirrorRole::Kind::Toplevel
                                   || parent->role().kind == MirrorRole::Kind::Toplevel);
    if (!parent_usable) {
        role = MirrorRole{MirrorRole::Kind::Toplevel, WindowId{}};
        parent = nullptr;
    }
    mirror->show_as(role, parent);
    follow_placement(*mirror, window);
}

void MirrorSession::follow_placement(WindowMirror& mirror, const GuestWindow& window) {
    if (mirror.role().kind != MirrorRole::Kind::Toplevel || !mirror.scale_known()) return;
    placement_.follow_guest(window, mirror.generation(), !window.minimized() && !window.maximized(),
                            mirror.guest_catching_up());
    if (window.maximized()) placement_.keep_on_guest_screen(window.id(), mirror.generation());
}

void MirrorSession::raise(const std::string& token) {
    for (const GuestWindow* window : windows().stacked()) {
        const Win32Style style{*window};
        if (style.disabled() || !style.activates() || !belongs(window->id())) continue;
        WindowMirror* mirror = find(window->id());
        if (mirror == nullptr || mirror->role().kind != MirrorRole::Kind::Toplevel) continue;
        if (window->minimized()) link_.send_sys_command(window->id(), SysCommand::Command::Restore);
        link_.send_activate(window->id(), true);
        mirror->activate(token);
        return;
    }
}

// Windows activates "the enabled popup window owned by" the window clicked
// (GW_ENABLEDPOPUP): an enabled window it owns, or owns through windows it
// owns, the topmost of them - a modal dialog's own modal dialog when there
// is one. Asked as the press on the disabled window, which is the latest.
void MirrorSession::bring_forward_for(WindowId window) {
    for (const GuestWindow* candidate : windows().stacked()) {
        if (Win32Style{*candidate}.disabled() || !owned_by(*candidate, window)) continue;
        if (WindowMirror* mirror = find(candidate->id())) {
            RWIN_DEBUG("win32", "{} brought forward for its disabled owner {}", candidate->id(), window);
            mirror->activate(std::string());
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

void MirrorSession::follow_grab(const PointerGrab& grab) {
    grab_ = grab;
    WindowMirror* mirror = grab.kind == PointerGrab::Kind::None ? nullptr : find(grab.window);
    const GuestWindow* window = windows().find(grab.window);
    grabber_.follow(grab, mirror, window == nullptr ? Point{} : window->picture_origin());
}

void MirrorSession::follow_pointer() {
    held_motion_.reset();
    const Globals& globals = kit_.surfaces.globals;
    wl_pointer* pointer = kit_.surfaces.seat.pointer();
    if (globals.relative_pointer() != nullptr && pointer != nullptr) {
        held_motion_ = std::make_unique<RelativePointer>(globals.relative_pointer(), pointer,
                                                         [this](PointF, PointF accelerated) {
                                                             for (const auto& [window, mirror] : mirrors_) {
                                                                 if (mirror->holds_press()) mirror->held_motion(accelerated);
                                                             }
                                                         });
    } else {
        RWIN_INFO("pointer", "no relative pointer: a window Windows drags is followed against its mirror");
    }
    for (const auto& [window, mirror] : mirrors_) mirror->follow_relative_motion(held_motion_ != nullptr);
}

void MirrorSession::note_ready() {
    if (ready_) return;
    ready_ = true;
    ready();
}

}  // namespace rwin::host
