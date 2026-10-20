#include "host/mirror/window_mirror.hpp"

#include "host/cursor/native_cursor_shape.hpp"
#include "host/input/guest_button.hpp"
#include "host/input/key_translator.hpp"
#include "host/input/pointer_grabber.hpp"
#include "host/latency/frame_stamps.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/guest_desktop.hpp"
#include "host/mirror/picture_limits.hpp"
#include "host/mirror/popup_layout.hpp"
#include "host/mirror/popup_stack.hpp"
#include "host/placement/placement_sync.hpp"
#include "host/wayland/activation.hpp"
#include "host/wayland/globals.hpp"
#include "host/wayland/key_press.hpp"
#include "host/wayland/seat.hpp"
#include "logging/logger.hpp"
#include "proto/pointer_at.hpp"
#include "proto/pointer_wheel.hpp"
#include "proto/rect.hpp"
#include "proto/sys_command.hpp"
#include "proto/window_icon.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace rwin::host {

namespace {

using Popup = WindowFields::Popup;

// A press this recent on the surface the keyboard comes to is what brought
// the keyboard there.
constexpr std::int64_t kPressBroughtFocusNs = 500'000'000;
// The first focus a surface gets this soon after it is mapped is the
// compositor focusing what it mapped.
constexpr std::int64_t kMapBroughtFocusNs = 500'000'000;
// A press older than this opened no popup: a grab with its serial would be
// refused, and the popup dismissed as it opens.
constexpr std::int64_t kGrabPressNs = 2'000'000'000;
// The compositor says a popup is dismissed before it hands on the press that
// dismissed it; a press this soon after, on a surface of this
// application's, is that press.
constexpr std::chrono::milliseconds kDismissingPress{80};

// Every surface made gets the next number: a surface made where a freed one
// was is still told apart from it.
std::uint64_t surfaces_made = 0;

bool opened_by_press(Popup::Kind kind) {
    return kind == Popup::Kind::MenuBar || kind == Popup::Kind::Submenu || kind == Popup::Kind::Combo
           || kind == Popup::Kind::Context;
}

}  // namespace

WindowMirror::WindowMirror(const MirrorKit& kit, const GuestWindow& window)
    : kit_(kit), window_(window), style_(window),
      moves_(
          kit.surfaces.loop, [this](const Box& frame) { ask_guest_for(frame); },
          [this](const Box& asked, const Box& got) { answered(asked, got); }),
      maximize_(
          [this](bool maximized) {
              kit_.link.send_sys_command(this->window(), maximized ? SysCommand::Command::Maximize
                                                                   : SysCommand::Command::Restore);
          },
          [this](bool maximized) {
              if (toplevel_ != nullptr) toplevel_->maximize(maximized);
          }),
      minimize_(
          [this](bool minimized) {
              kit_.link.send_sys_command(this->window(), minimized ? SysCommand::Command::Minimize
                                                                   : SysCommand::Command::Restore);
          },
          [this](bool minimized) { minimize_here(minimized); }),
      dismissal_(kit.surfaces.loop.timer([this] { dismiss_in_guest(); })) {
    limits_ = window.limits();
    picture_ = window.picture_size();
    moves_.reported(window.frame());
}

WindowMirror::~WindowMirror() { drop_surface(); }

wl_surface* WindowMirror::surface() const noexcept { return surface_ != nullptr ? surface_->surface() : nullptr; }

xdg_surface* WindowMirror::xdg() const noexcept {
    if (toplevel_ != nullptr) return toplevel_->xdg();
    if (popup_ != nullptr) return popup_->xdg();
    return nullptr;
}

const WindowMirror* WindowMirror::xdg_owner(Point& offset) const noexcept {
    if (toplevel_ != nullptr || popup_ != nullptr) return this;
    if (subsurface_ == nullptr || parent_ == nullptr) return nullptr;
    offset = offset + placed_.corner();
    return parent_->xdg_owner(offset);
}

double WindowMirror::scale() const noexcept {
    if (surface_ != nullptr) return surface_->scale();
    return parent_ != nullptr ? parent_->scale() : kit_.surfaces.globals.output_scale();
}

ViewMapping WindowMirror::mapping() const noexcept {
    if (surface_ != nullptr && !surface_->picture_size().empty()) return surface_->mapping();
    return ViewMapping::one_to_one(window_.picture_size(), scale());
}

Size WindowMirror::picture_size() const noexcept {
    if (surface_ != nullptr && !surface_->picture_size().empty()) return surface_->picture_size();
    return window_.picture_size();
}

Size WindowMirror::logical_of(Size picture) const noexcept {
    return ViewMapping::one_to_one(picture, scale()).logical();
}

Size WindowMirror::shown_picture() const noexcept {
    return surface_ != nullptr && !surface_->picture_size().empty() ? surface_->picture_size() : window_.picture_size();
}

Size WindowMirror::window_size() const noexcept { return wanted_.value_or(logical_of(picture_)); }

// The same role against the same parent's same surface keeps its surface,
// and is only placed again.
void WindowMirror::show_as(const MirrorRole& role, WindowMirror* parent) {
    const bool same = surface_ != nullptr && role == role_ && parent == parent_
                      && (parent == nullptr || (parent->shown() && parent->surface_serial_ == made_against_));
    role_ = role;
    parent_ = parent;
    if (dismissed_) return;
    if (same) {
        if (popup_ != nullptr) place_popup();
        if (subsurface_ != nullptr) place_subsurface();
        if (toplevel_ != nullptr) toplevel_->set_parent(parent != nullptr ? parent->toplevel_.get() : nullptr);
        return;
    }
    drop_surface();
    // A popup or a part waits for what it is placed against to be on screen.
    if (role.kind != MirrorRole::Kind::Toplevel && (parent == nullptr || !parent->mapped())) return;
    make_surface();
}

void WindowMirror::hide() {
    dismissed_ = false;
    drop_surface();
}

void WindowMirror::orphan() {
    drop_surface();
    parent_ = nullptr;
    role_ = MirrorRole{};
}

// The stack takes popups down topmost first, and this one is topmost by
// now: nothing more is to be asked of it.
void WindowMirror::drop_popup() {
    if (popup_ == nullptr) return;
    kit_.grabber.forget(*this);
    if (buttons_held_ > 0) send_button(pressed_button_, pressed_at_, false);
    buttons_held_ = 0;
    held_at_.reset();
    popup_.reset();
    kit_.activation.forget(surface_->surface());
    surface_.reset();
    made_against_ = 0;
    pointer_.reset();
    dropped(window());
}

void WindowMirror::make_surface() {
    surface_ = std::make_unique<MirrorSurface>(kit_.surfaces, *this);
    surface_serial_ = ++surfaces_made;
    made_against_ = parent_ != nullptr ? parent_->surface_serial_ : 0;
    if (parent_ != nullptr) surface_->fall_back_to(parent_->scale());
    surface_->rescaled.connect([this] { rescaled(); });
    surface_->picture_resized.connect([this] { picture_changed(); });
    surface_->shared_refused.connect([this](const std::string& why) { shared_refused(window(), why); });
    surface_->presented.connect([this](const FrameStamps& stamps) { presented(stamps); });
    surface_->first_mapped.connect([this] {
        mapped_at_ = now_ns();
        mapped_now(window());
    });
    mapped_at_ = 0;
    focused_since_mapped_ = false;
    surface_->frame_owed.connect([this] { show_last_frame(); });
    surface_->set_opacity(style_.opacity());
    surface_->take_no_input(style_.click_through());
    surface_->shape_input(window_.region());
    surface_->shape_opaque(window_.opaque_region(), window_.picture_size());
    make_role();
    show_last_frame();
}

void WindowMirror::make_role() {
    switch (role_.kind) {
        case MirrorRole::Kind::Toplevel:
            toplevel_ = std::make_unique<ToplevelRole>(kit_.surfaces.globals, *surface_, *this, false);
            ++generation_;
            dress_toplevel();
            toplevel_->start();
            RWIN_INFO("mirror", "{} is a toplevel{}", window(), parent_ != nullptr ? " kept above its owner" : "");
            remade(window());
            return;
        case MirrorRole::Kind::Popup:
            place_popup();
            return;
        case MirrorRole::Kind::Subsurface:
            subsurface_ = std::make_unique<SubsurfaceRole>(kit_.surfaces.globals, *surface_, parent_->surface());
            if (window_.fields().z_glue) subsurface_->below_parent();
            place_subsurface();
            RWIN_INFO("mirror", "{} is a part of {}", window(), parent_->window());
            return;
    }
}

void WindowMirror::show_last_frame() {
    if (last_shared_.has_value()) {
        surface_->show(*last_shared_);
    } else if (!last_pixels_.empty()) {
        surface_->show(PixelsView{last_pixels_.data(), last_pixels_size_.width, last_pixels_size_.height,
                                  last_pixels_size_.width * 4});
    }
}

// Children first: a popup under this one goes before it, and every popup
// made after it - the compositor takes popups down only from the top.
void WindowMirror::drop_surface() {
    if (surface_ == nullptr) return;
    kit_.grabber.forget(*this);
    kit_.popups.closing_under(*this);
    if (popup_ != nullptr) kit_.popups.closing(*this);
    if (buttons_held_ > 0) send_button(pressed_button_, pressed_at_, false);
    buttons_held_ = 0;
    held_at_.reset();
    inhibitor_.reset();
    lock_holds_shortcuts_ = false;
    backdrop_.reset();
    geometry_corner_ = Point{};
    popup_.reset();
    subsurface_.reset();
    toplevel_.reset();
    kit_.activation.forget(surface_->surface());
    surface_.reset();
    made_against_ = 0;
    pointer_.reset();
    configure_ = ToplevelConfigure{};
    caption_.clear();
    asked_fullscreen_ = false;
    minimize_owed_ = false;
    wanted_.reset();
    resize_edges_ = 0;
}

void WindowMirror::dress_toplevel() {
    if (!kit_.app_id.empty()) toplevel_->set_app_id(kit_.app_id);
    caption_ = caption();
    toplevel_->set_title(caption_);
    toplevel_->set_parent(parent_ != nullptr ? parent_->toplevel_.get() : nullptr);
    apply_limits();
    if (icon_ != nullptr) icon_->put_on(*toplevel_);
    const std::int64_t now = now_ns();
    maximize_.compositor_anew(now);
    minimize_.compositor_anew(now);
    follow_state();
}

// The title, and the tag the compositor knows the window by, saying
// whether this desktop's taskbar and switcher leave it out.
std::string WindowMirror::caption() const {
    return (window_.title().empty() ? std::string("Guest window") : window_.title())
           + kit_.placement.caption_tag(window(), KWinPlacement::Skips{style_.off_taskbar(), style_.off_switcher()});
}

void WindowMirror::place_popup() {
    if (parent_ == nullptr || surface_ == nullptr) return;
    Point offset;
    const WindowMirror* owner = parent_->xdg_owner(offset);
    if (owner == nullptr || owner->xdg() == nullptr) return;
    PopupPlacement placement =
        popup_layout(window_, parent_->window_, parent_->mapping(), logical_of(window_.picture_size()));
    // A positioner's anchor is on its parent's window geometry, which on a
    // picture centred in a larger window starts left of and above it.
    placement.anchor = placement.anchor.translated(offset - owner->geometry_corner_);
    if (popup_ != nullptr) {
        if (popup_->repositions()) popup_->reposition(placement);
        return;
    }
    const Popup::Kind kind = window_.fields().popup.kind;
    const InputSerial& press = kit_.surfaces.seat.last_press();
    const bool may_grab = owner->toplevel_ != nullptr || owner->grabs();
    const bool grab = opened_by_press(kind) && may_grab && press.valid() && now_ns() - press.at < kGrabPressNs
                      && !kit_.popups.open_beside(*parent_);
    popup_ = std::make_unique<PopupRole>(kit_.surfaces.globals, *surface_, owner->xdg(), placement,
                                         kit_.surfaces.seat.seat(), grab ? press.serial : 0, *this);
    kit_.popups.opened(*this);
    RWIN_INFO("popup", "{} opens from {} ({}): anchor {},{} {}x{} edge {} gravity {} adjust {:#x} offset {},{}{}",
              window(), parent_->window(), static_cast<int>(kind), placement.anchor.x, placement.anchor.y,
              placement.anchor.width, placement.anchor.height, placement.edge, placement.gravity, placement.adjust,
              placement.offset.x, placement.offset.y, grab ? ", grabbing" : "");
}

void WindowMirror::place_subsurface() {
    if (subsurface_ == nullptr || parent_ == nullptr) return;
    const Point at = subsurface_layout(window_, parent_->window_, parent_->mapping());
    placed_ = Box::of(at, mapping().logical());
    if (subsurface_->place(at) && parent_->surface_ != nullptr) parent_->surface_->commit();
}

// A frame of another size has come, or the scale changed: it is shown
// within the window as it is.
void WindowMirror::picture_changed() {
    if (toplevel_ != nullptr) {
        apply_limits();
        if (toplevel_->configured()) fit_into(configure_);
    }
    if (popup_ != nullptr) place_popup();
    if (subsurface_ != nullptr) place_subsurface();
    reshaped(window());
}

// At another scale the same logical size is another picture: the guest is
// asked for it, as for a size the compositor gave.
void WindowMirror::rescaled() {
    if (toplevel_ != nullptr && style_.sizable() && !configure_.size.empty() && !configure_.maximized
        && !configure_.fullscreen) {
        wanted_ = configure_.size;
        ask_for_wanted();
    }
    picture_changed();
}

// The guest said its window is another size: the size the compositor gave,
// once the guest has it, is the window's own; a size the guest took by
// itself is the window's from now on.
void WindowMirror::resized_in_guest() {
    if (toplevel_ == nullptr || !toplevel_->configured()) return;
    const Size natural = logical_of(picture_);
    if (wanted_.has_value() && natural == *wanted_) {
        wanted_.reset();
    } else if (!wanted_.has_value() && !configure_.size.empty() && !configure_.maximized && !configure_.fullscreen
               && !window_.maximized() && natural != configure_.size) {
        resized_by_guest(window());
    }
    apply_limits();
    fit_into(configure_);
}

void WindowMirror::follow(const GuestWindow& window) {
    window_ = window;
    // A minimised window's frame is the stub Windows parks off screen: the
    // window keeps the size it had.
    const Size before = picture_;
    if (!window.minimized() && !window.picture_size().empty()) picture_ = window.picture_size();
    style_ = Win32Style{window};
    limits_ = window.limits();
    moves_.reported(window.frame());
    const ViewMapping view = mapping();
    const GuestWindow::Margins margins = window.margins();
    const Box band = view.logical_box(Box{0, 0, margins.left, margins.top});
    const Box far = view.logical_box(Box{0, 0, margins.right, margins.bottom});
    border_.fit_to(band.width, band.height, far.width, far.height);
    if (surface_ == nullptr) return;
    surface_->set_opacity(style_.opacity());
    surface_->take_no_input(style_.click_through());
    surface_->shape_input(window.region());
    surface_->shape_opaque(window.opaque_region(), window.picture_size());
    if (toplevel_ != nullptr) {
        if (std::string now = caption(); now != caption_) {
            caption_ = std::move(now);
            toplevel_->set_title(caption_);
        }
        toplevel_->set_modal(parent_ != nullptr && parent_->style_.disabled() && !style_.disabled());
        apply_limits();
        follow_state();
        if (picture_ != before) resized_in_guest();
        ask_for_wanted();
    }
    if (popup_ != nullptr) place_popup();
    if (subsurface_ != nullptr) place_subsurface();
    surface_->commit();
}

// What the window answered WM_GETMINMAXINFO, applied side by side
// (PictureLimits); a fixed size for a window with no sizing border.
void WindowMirror::apply_limits() {
    if (toplevel_ == nullptr) return;
    if (window_.maximized() || asked_fullscreen_ || configure_.maximized || configure_.fullscreen) {
        toplevel_->limit(Size{}, Size{});
        return;
    }
    if (!style_.sizable()) {
        const Size fixed = logical_of(picture_);
        toplevel_->limit(fixed, fixed);
        return;
    }
    const PictureLimits picture =
        PictureLimits::of(limits_, window_.margins(), window_.picture_padding(), kit_.desktop.work_area.size());
    toplevel_->limit(logical_limit(picture.smallest), logical_limit(picture.largest));
}

// A side of no limit stays one: logical_of makes the least picture a unit.
Size WindowMirror::logical_limit(Size picture) const noexcept {
    const Size logical = logical_of(picture);
    return Size{picture.width > 0 ? logical.width : 0, picture.height > 0 ? logical.height : 0};
}

// The guest window's state, asked of the compositor: minimised, maximised,
// fullscreen - a window with no caption and no sizing border covering the
// guest's monitor, which on Windows is how a game is fullscreen. Where
// nothing reports the compositor's minimising (off KWin), it is taken to
// have done as asked.
void WindowMirror::follow_state() {
    if (toplevel_ == nullptr) return;
    const std::int64_t now = now_ns();
    minimize_.guest_says(window_.minimized(), now);
    if (!kit_.placement.active()) minimize_.compositor_says(minimize_.compositor_expected(), now);
    maximize_.guest_says(window_.maximized(), now);
    const bool full = covers_monitor();
    if (full != asked_fullscreen_) {
        asked_fullscreen_ = full;
        toplevel_->fullscreen(full);
        inhibit_shortcuts();
        RWIN_INFO("mirror", "{} {} fullscreen", window(), full ? "is" : "is no longer");
    }
}

// A client cannot unminimise itself; it can ask to be activated, which
// brings a window back.
void WindowMirror::minimize_here(bool minimized) {
    if (toplevel_ == nullptr) return;
    if (!minimized) {
        if (!configure_.activated) kit_.activation.hand_to(surface());
        return;
    }
    if (toplevel_->configured()) {
        toplevel_->minimize();
    } else {
        minimize_owed_ = true;
    }
}

void WindowMirror::compositor_minimized(bool minimized) {
    RWIN_DEBUG("mirror", "{} {} by the compositor", window(), minimized ? "minimised" : "unminimised");
    minimize_.compositor_says(minimized, now_ns());
}

void WindowMirror::hold_shortcuts_for_lock(bool held) {
    lock_holds_shortcuts_ = held;
    inhibit_shortcuts();
}

// One inhibitor a surface and seat, whichever reasons want it: a second is
// a protocol error (already_inhibited), and fullscreen with the pointer
// locked - a game - is both.
void WindowMirror::inhibit_shortcuts() {
    const bool wanted = surface_ != nullptr && (asked_fullscreen_ || lock_holds_shortcuts_)
                        && kit_.surfaces.globals.shortcuts_inhibit() != nullptr;
    if (!wanted) {
        inhibitor_.reset();
    } else if (inhibitor_ == nullptr) {
        inhibitor_ = std::make_unique<ShortcutsInhibitor>(kit_.surfaces.globals.shortcuts_inhibit(), surface(),
                                                          kit_.surfaces.seat.seat());
    }
}

bool WindowMirror::covers_monitor() const noexcept {
    const Box& monitor = kit_.desktop.monitor;
    if (monitor.empty() || window_.hidden() || window_.minimized() || window_.maximized()) return false;
    if (style_.has_caption() || style_.sizable()) return false;
    return window_.frame().contains(monitor);
}

void WindowMirror::configured(const ToplevelConfigure& configure) {
    const ToplevelConfigure before = configure_;
    configure_ = configure;
    if (std::exchange(minimize_owed_, false)) toplevel_->minimize();
    if (!configure.resizing) resize_edges_ = 0;
    maximize_.compositor_says(configure.maximized, now_ns());
    take_size(configure, before);
    fit_into(configure);
    if (!before.activated && configure.activated) RWIN_DEBUG("focus", "{} activated by the compositor", window());
}

// A size the compositor gives that it did not give before - a step of a
// drag, a restore, a tiling - is the window's: asked of the guest. One it
// says again is not news: the guest may have taken another since.
void WindowMirror::take_size(const ToplevelConfigure& configure, const ToplevelConfigure& before) {
    if (configure.maximized || configure.fullscreen || configure.size.empty() || !style_.sizable()) {
        wanted_.reset();
        return;
    }
    if (configure.size == before.size && !before.maximized && !before.fullscreen) return;
    wanted_ = configure.size;
    ask_for_wanted();
}

// Not while the guest window is maximised or minimised, or on its way to or
// from either: Windows sizes it by its state then, and a move would land on
// the window in a state it is leaving.
void WindowMirror::ask_for_wanted() {
    if (!wanted_.has_value() || toplevel_ == nullptr) return;
    if (logical_of(picture_) == *wanted_) {
        wanted_.reset();
        return;
    }
    if (window_.maximized() || window_.minimized() || maximize_.guest_heading() || minimize_.guest_heading()) return;
    const Size picture = ViewMapping::picture_for(*wanted_, scale());
    const Size padding = window_.picture_padding();
    moves_.want_size(Size{picture.width + padding.width, picture.height + padding.height}, resize_edges_);
}

// The guest made its window another size than it was asked - its smallest,
// the most its work area holds - and that is the window's size: the
// compositor is shown it.
void WindowMirror::answered(const Box& asked, const Box& got) {
    if (!moves_.idle() || !wanted_.has_value() || got.size() == asked.size()) return;
    RWIN_DEBUG("mirror", "{} asked {}x{}, made {}x{}: the guest's size is taken", window(), asked.width,
               asked.height, got.width, got.height);
    wanted_.reset();
    if (toplevel_ != nullptr && toplevel_->configured()) fit_into(configure_);
}

// The window is as large as the guest says its picture is, and the frames
// are shown in it one to one from its corner: cropped where a frame is
// larger, over black bars where it is smaller - a frame from before a
// resize, one Windows captured as the window was minimised. A size the
// compositor gave and the guest has not answered yet is the window's,
// meanwhile, the same way: the window follows the compositor at once, and
// the guest's picture catches up within it. Maximised or fullscreen the
// compositor's size is the window's, the frame centred on it.
void WindowMirror::fit_into(const ToplevelConfigure& configure) {
    if (toplevel_ == nullptr || surface_ == nullptr) return;
    const Size shown = logical_of(shown_picture());
    geometry_corner_ = Point{};
    if ((configure.maximized || configure.fullscreen) && !configure.size.empty()) {
        fit_given(configure.size, shown);
    } else {
        const Size window = window_size();
        surface_->crop_to(window);
        if (shown.width < window.width || shown.height < window.height) {
            if (backdrop_ == nullptr) backdrop_ = std::make_unique<Backdrop>(kit_.surfaces.globals, surface());
            backdrop_->cover(Box::of(Point{}, window));
        } else {
            backdrop_.reset();
        }
        toplevel_->set_window_geometry(Box::of(Point{}, window));
    }
    surface_->commit();
}

void WindowMirror::fit_given(Size given, Size natural) {
    if (natural == given) {
        backdrop_.reset();
        surface_->fit_one_to_one();
        toplevel_->set_window_geometry(Box::of(Point{}, natural));
    } else if (natural.width <= given.width && natural.height <= given.height) {
        surface_->fit_one_to_one();
        const Point corner{(given.width - natural.width) / 2, (given.height - natural.height) / 2};
        const Box whole{-corner.x, -corner.y, given.width, given.height};
        if (backdrop_ == nullptr) backdrop_ = std::make_unique<Backdrop>(kit_.surfaces.globals, surface());
        backdrop_->cover(whole);
        toplevel_->set_window_geometry(whole);
        geometry_corner_ = whole.corner();
    } else {
        backdrop_.reset();
        surface_->fit_to(given);
        toplevel_->set_window_geometry(Box::of(Point{}, given));
    }
}

void WindowMirror::close_requested() {
    // CS_NOCLOSE: Windows would not act on this, so neither does the mirror.
    if (!style_.closable()) {
        RWIN_DEBUG("win32", "{} does not close: CS_NOCLOSE", window());
        return;
    }
    // Closed by the guest, if it closes: an application that hides to its
    // tray, or asks first, stays - and so does its mirror.
    RWIN_DEBUG("win32", "{} closed here; asking the guest to close it", window());
    kit_.link.send_sys_command(window(), SysCommand::Command::Close);
}

void WindowMirror::popup_placed(const Box& where) { placed_ = where; }

// A press on another application's window reaches nothing here: the guest
// is told to close the popup. A press on one of this application's own
// surfaces reaches the guest, which closes it - or opens the next menu of a
// menu bar - by itself; told to close it as well, it would close that one.
void WindowMirror::popup_dismissed() {
    drop_surface();
    dismissed_ = true;
    dismissed_at_ = now_ns();
    dismissal_->start(kDismissingPress, false);
}

void WindowMirror::dismiss_in_guest() {
    const InputSerial& press = kit_.surfaces.seat.last_press();
    if (press.valid() && press.at >= dismissed_at_) {
        RWIN_INFO("popup", "the compositor dismissed {} for a press of this application's; the guest closes it", window());
        return;
    }
    RWIN_INFO("popup", "the compositor dismissed {}; closing it in the guest", window());
    kit_.link.send_popup_dismiss(window());
}

void WindowMirror::ask_guest_for(const Box& frame) {
    RWIN_DEBUG("mirror", "{} to {},{} {}x{} guest pixels", window(), frame.x, frame.y, frame.width, frame.height);
    kit_.link.send_window_move(window(), Rect{frame.x, frame.y, static_cast<std::uint32_t>(frame.width),
                                              static_cast<std::uint32_t>(frame.height)});
}

// Not while either side has the window in a state, or is on its way into
// or out of one: the frame a state gives is no place for the guest window,
// which Windows keeps one of its own for. KWin reports a maximised frame
// before its maximise mode, so its report alone cannot tell.
void WindowMirror::move_guest_to(Point picture_corner) {
    if (configure_.maximized || configure_.fullscreen || window_.maximized() || window_.minimized()
        || maximize_.guest_heading() || minimize_.guest_heading()) {
        RWIN_DEBUG("placement", "{} is in a state, or on its way to one: not moved to {},{}", window(),
                   picture_corner.x, picture_corner.y);
        return;
    }
    moves_.want_corner(picture_corner - window_.picture_offset());
}

void WindowMirror::show_frame(const PixelsView& pixels) {
    ++frames_;
    last_shared_.reset();
    const Size size{pixels.width, pixels.height};
    if (size != last_pixels_size_) {
        last_pixels_.resize(static_cast<std::size_t>(size.width) * size.height * 4);
        last_pixels_size_ = size;
    }
    for (int row = 0; row < size.height; ++row) {
        std::memcpy(last_pixels_.data() + static_cast<std::size_t>(row) * size.width * 4,
                    pixels.bgra + static_cast<std::size_t>(row) * pixels.stride, static_cast<std::size_t>(size.width) * 4);
    }
    if (surface_ != nullptr) surface_->show(pixels);
}

void WindowMirror::show_frame(const SharedFrame& frame) {
    ++frames_;
    last_pixels_.clear();
    last_pixels_size_ = Size{};
    last_shared_ = frame;
    if (surface_ != nullptr) surface_->show(frame);
}

void WindowMirror::set_icon(const WindowIcon& icon) {
    RWIN_DEBUG("icons", "{}'s icon, {}x{}", window(), icon.width, icon.height);
    icon_ = std::make_unique<ToplevelIcon>(kit_.surfaces.globals, icon);
    if (toplevel_ == nullptr) return;
    icon_->put_on(*toplevel_);
    surface_->commit();
}

void WindowMirror::note_guest_active(bool active) { guest_active_ = active; }

void WindowMirror::activate(const std::string& token) {
    if (surface_ == nullptr || toplevel_ == nullptr || !style_.activates()) return;
    if (token.empty()) {
        kit_.activation.hand_to(surface());
    } else {
        kit_.activation.activate(surface(), token);
    }
}

// A keyboard move or size has no press to give back, and Windows runs it
// against the window's own geometry, which the mirror follows.
void WindowMirror::take_over_move_size(MoveSizeKind kind) {
    if (toplevel_ == nullptr || buttons_held_ == 0) {
        RWIN_DEBUG("chrome", "{}: Windows moves or sizes it with no press held here; left to Windows", window());
        return;
    }
    if (kind == MoveSizeKind::KeyMove || kind == MoveSizeKind::KeySize) return;
    const InputSerial& press = kit_.surfaces.seat.last_press();
    if (!press.valid() || press.surface != surface()) return;
    if (const std::optional<std::uint32_t> edges = ResizeBorder::edges_of(kind)) {
        resize_edges_ = *edges;
        toplevel_->resize(kit_.surfaces.seat.seat(), press.serial, *edges);
    } else {
        toplevel_->move(kit_.surfaces.seat.seat(), press.serial);
    }
    drag_handed(window());
    // The compositor owns the release now, which never reaches this
    // surface: the guest's press is let go at once, where it was made.
    send_button(pressed_button_, pressed_at_, false);
    buttons_held_ = 0;
    held_at_.reset();
    swallow_release_ = true;
    let_go(window());
}

std::optional<Point> WindowMirror::picture_point(PointF at) const noexcept {
    if (surface_ == nullptr) return std::nullopt;
    const Point point = surface_->mapping().picture_point(at);
    if (buttons_held_ > 0) return point;
    const Size picture = surface_->picture_size();
    if (!Box::of(Point{}, picture).contains(point)) return std::nullopt;
    return point;
}

void WindowMirror::send_button(std::uint8_t button, Point at, bool pressed) {
    kit_.link.send_pointer_button(window(), at, button, pressed);
}

void WindowMirror::wear_cursor(const CursorLook* look, std::size_t step) {
    cursor_hidden_ = look == nullptr;
    look_ = look;
    step_ = look == nullptr || look->frames.empty() ? 0 : step % look->frames.size();
    apply_cursor();
}

// Not while the pointer is in the resize band: there the shape says the
// edge can be dragged, and the guest has no idea that band exists.
void WindowMirror::apply_cursor() {
    if (surface_ == nullptr || kit_.surfaces.seat.pointer_focus() != surface()) return;
    PointerCursor& cursor = kit_.surfaces.seat.cursor();
    if (hovered_edges_ != 0 && cursor.draws_shapes()) {
        cursor.wear(ResizeBorder::cursor_for(hovered_edges_));
        return;
    }
    if (cursor_hidden_) {
        cursor.wear_nothing();
        return;
    }
    if (look_ == nullptr || look_->frames.empty()) {
        cursor.wear(CursorShape::Default);
        return;
    }
    if (const std::optional<CursorShape> shape = native_shape_for(look_->system); shape && cursor.draws_shapes()) {
        cursor.wear(*shape);
        return;
    }
    cursor.wear(look_->id, step_, look_->frames[step_], scale());
}

void WindowMirror::pointer_entered(PointF at) {
    pointer_moved(at);
    apply_cursor();
}

void WindowMirror::pointer_moved(PointF at) {
    pointer_ = at;
    if (style_.disabled() || surface_ == nullptr) return;
    const bool resizable = toplevel_ != nullptr && style_.sizable() && !configure_.maximized && !configure_.fullscreen;
    const std::uint32_t edges = resizable ? border_.at(at, window_size()) : 0;
    if (edges != hovered_edges_) {
        hovered_edges_ = edges;
        apply_cursor();
    }
    // Held, the pointer's own motion moves the guest's (held_motion); held
    // still by a lock, only its motion reaches the guest at all.
    if (held_at_.has_value() || kit_.grabber.locked_on(*this)) return;
    if (const std::optional<Point> point = picture_point(at)) kit_.link.send_pointer_move(window(), *point);
}

// Not while a button is held: the guest holds the capture, and goes on
// being told where the pointer is until it comes up.
void WindowMirror::pointer_left() {
    pointer_.reset();
    hovered_edges_ = 0;
    if (buttons_held_ > 0 || style_.disabled()) return;
    kit_.link.send_pointer_leave(window());
}

void WindowMirror::button(std::uint32_t code, bool pressed) {
    const std::optional<std::uint8_t> guest = guest_button(code);
    if (!guest.has_value() || !pointer_.has_value()) return;
    if (!pressed) {
        if (swallow_release_) {
            swallow_release_ = false;
            return;
        }
        // A release the guest saw the press of goes to it wherever it
        // happens: left out, the guest holds the button down for good.
        if (buttons_held_ == 0) return;
        --buttons_held_;
        if (held_at_.has_value()) {
            kit_.link.send_pointer_at(PointerAt{window(), static_cast<std::int32_t>(std::lround(held_at_->x)),
                                                static_cast<std::int32_t>(std::lround(held_at_->y)), *guest, false});
        } else if (const std::optional<Point> at = picture_point(*pointer_)) {
            send_button(*guest, *at, false);
        }
        if (buttons_held_ > 0) return;
        held_at_.reset();
        let_go(window());
        return;
    }
    // WS_DISABLED: "A disabled window cannot receive input from the user."
    if (style_.disabled()) {
        pressed_while_disabled(window());
        return;
    }
    button_pressed(window());
    if (kit_.grabber.intercept_press(*this)) return;
    swallow_release_ = false;
    const InputSerial& press = kit_.surfaces.seat.last_press();
    if (code == BTN_LEFT && hovered_edges_ != 0 && toplevel_ != nullptr && press.surface == surface()) {
        RWIN_DEBUG("chrome", "{} resized from edges {:#x}", window(), hovered_edges_);
        resize_edges_ = hovered_edges_;
        toplevel_->resize(kit_.surfaces.seat.seat(), press.serial, hovered_edges_);
        swallow_release_ = true;
        drag_handed(window());
        return;
    }
    const std::optional<Point> at = picture_point(*pointer_);
    if (!at.has_value()) return;
    ++buttons_held_;
    pressed_at_ = *at;
    pressed_button_ = *guest;
    send_button(*guest, *at, true);
    if (relative_motion_ && !held_at_.has_value()) {
        const Point screen = window_.picture_origin() + *at;
        held_at_ = PointF{static_cast<double>(screen.x), static_cast<double>(screen.y)};
    }
}

// Logical units of this surface to the guest's pixels.
void WindowMirror::held_motion(PointF logical) {
    if (!held_at_.has_value()) return;
    held_at_->x += logical.x * scale();
    held_at_->y += logical.y * scale();
    kit_.link.send_pointer_at(PointerAt{window(), static_cast<std::int32_t>(std::lround(held_at_->x)),
                                        static_cast<std::int32_t>(std::lround(held_at_->y)), PointerAt::kNoButton,
                                        false});
}

// 120ths of a notch, which is Windows' own WHEEL_DELTA; Windows' vertical
// wheel is positive away from the user, Wayland's towards.
void WindowMirror::wheel(Wheel turned) {
    if (style_.disabled() || !pointer_.has_value()) return;
    const std::optional<Point> at = picture_point(*pointer_);
    if (!at.has_value()) return;
    kit_.link.send_pointer_wheel(PointerWheel{window(), at->x, at->y, turned.dx120, -turned.dy120});
}

bool WindowMirror::pressed_into() const noexcept {
    const InputSerial& press = kit_.surfaces.seat.last_press();
    return press.valid() && press.surface == surface() && now_ns() - press.at < kPressBroughtFocusNs;
}

// A window minimised in the guest that the user brings back here is
// restored there: Windows does not restore a window for being activated.
void WindowMirror::keyboard_entered() {
    kit_.grabber.keyboard_moved();
    if (toplevel_ == nullptr || !style_.activates()) return;
    if (window_.minimized()) minimize_.want_guest(false, now_ns());
    focused(window(), focus_cause());
}

FocusCause WindowMirror::focus_cause() noexcept {
    const bool first = !std::exchange(focused_since_mapped_, true);
    if (pressed_into()) return FocusCause::Press;
    if (first && mapped_at_ != 0 && now_ns() - mapped_at_ < kMapBroughtFocusNs) return FocusCause::Mapping;
    return FocusCause::Other;
}

void WindowMirror::keyboard_left() {
    held_keys_.release_all(kit_.link, window());
    kit_.grabber.keyboard_moved();
}

// A held key repeats here as presses alone, as Windows repeats one.
void WindowMirror::key(const KeyPress& press) {
    if (kit_.grabber.intercept(press) || style_.disabled()) return;
    const std::optional<KeyStroke> stroke = stroke_for(press, window());
    if (!stroke.has_value()) return;
    held_keys_.note(*stroke);
    kit_.link.send_key(*stroke);
}

MirrorState WindowMirror::state() const {
    MirrorState state;
    state.window = window();
    state.role = role_;
    state.shown = surface_ != nullptr;
    state.mapped = mapped();
    state.on_gpu = surface_ != nullptr && surface_->on_gpu();
    state.activated = configure_.activated;
    state.maximized = configure_.maximized;
    state.fullscreen = configure_.fullscreen;
    state.asked_minimized = minimize_.compositor_expected();
    state.asked_maximized = maximize_.compositor_expected();
    state.asked_fullscreen = asked_fullscreen_;
    state.modal = toplevel_ != nullptr && parent_ != nullptr && parent_->style_.disabled() && !style_.disabled();
    state.grabs = grabs();
    state.placed = placed_;
    state.title = window_.title();
    state.logical = mapping().logical();
    state.scale = scale();
    state.picture = surface_ != nullptr ? surface_->picture_size() : Size{};
    state.frames = frames_;
    state.generation = generation_;
    return state;
}

}  // namespace rwin::host
