#include "host/input/pointer_grabber.hpp"

#include "host/geometry/box.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/window_mirror.hpp"
#include "host/wayland/globals.hpp"
#include "host/wayland/key_press.hpp"
#include "host/wayland/seat.hpp"
#include "logging/logger.hpp"

#include <linux/input-event-codes.h>

#include <cmath>

namespace rwin::host {

namespace {

const char* kind_name(PointerGrab::Kind kind) {
    switch (kind) {
        case PointerGrab::Kind::Confine: return "confine";
        case PointerGrab::Kind::Lock: return "lock";
        case PointerGrab::Kind::None: break;
    }
    return "none";
}

}  // namespace

PointerGrabber::PointerGrabber(GuestLink& link, const Globals& globals, Seat& seat)
    : link_(link), globals_(globals), seat_(seat) {}

PointerGrabber::~PointerGrabber() = default;

bool PointerGrabber::wanted() const noexcept { return grab_.kind != PointerGrab::Kind::None && mirror_ != nullptr; }

void PointerGrabber::follow(const PointerGrab& grab, WindowMirror* mirror, Point guest_origin) {
    if (grab == grab_ && mirror == mirror_ && guest_origin == origin_) return;
    RWIN_INFO("pointer", "guest wants {} for {}", kind_name(grab.kind), grab.window);
    const bool new_pattern = grab.kind != grab_.kind || grab.window != grab_.window;
    let_go();
    grab_ = grab;
    origin_ = guest_origin;
    mirror_ = mirror;
    if (new_pattern) escaped_ = false;
    if (wanted() && !escaped_ && mirror_->surface() != nullptr && seat_.keyboard_focus() == mirror_->surface()) hold();
}

void PointerGrabber::forget(const WindowMirror& mirror) {
    if (&mirror != mirror_) return;
    let_go();
    mirror_ = nullptr;
}

void PointerGrabber::keyboard_moved() {
    notice_compositor_let_go();
    if (mirror_ == nullptr) return;
    const bool focused = mirror_->surface() != nullptr && seat_.keyboard_focus() == mirror_->surface();
    if (!focused) {
        let_go();
    } else if (wanted() && !escaped_ && lock_ == nullptr && confinement_ == nullptr) {
        hold();
    }
}

void PointerGrabber::hold() {
    wl_surface* surface = mirror_->surface();
    wl_pointer* pointer = seat_.pointer();
    const char* unavailable = surface == nullptr || pointer == nullptr       ? "no surface or pointer"
                              : globals_.pointer_constraints() == nullptr ? "the compositor has no pointer constraints"
                              : grab_.kind == PointerGrab::Kind::Lock && globals_.relative_pointer() == nullptr
                                  ? "the compositor has no relative pointer"
                                  : nullptr;
    if (unavailable != nullptr) {
        RWIN_INFO("pointer", "cannot {} the pointer on {}: {}", kind_name(grab_.kind), grab_.window, unavailable);
        return;
    }
    const ViewMapping view = mirror_->mapping();
    if (grab_.kind == PointerGrab::Kind::Confine) {
        // The part of the guest's clip the mirror shows, rounded inward: a
        // pointer kept inside it is over guest pixels inside the clip.
        const Box clip{grab_.area.left - origin_.x, grab_.area.top - origin_.y, grab_.area.width(), grab_.area.height()};
        const Box inside = view.logical_box_inside(clip.intersected(Box::of(Point{}, mirror_->picture_size())));
        if (inside.empty()) {
            RWIN_INFO("pointer", "the clip of {} covers no whole unit of its mirror", grab_.window);
            return;
        }
        confinement_ = std::make_unique<ConfinedPointer>(globals_.compositor(), globals_.pointer_constraints(),
                                                         surface, pointer, inside);
        RWIN_INFO("pointer", "asked the compositor to confine the pointer on {} ({},{} {}x{})", grab_.window,
                  inside.x, inside.y, inside.width, inside.height);
        return;
    }
    std::optional<PointF> hint;
    const Point at{grab_.x - origin_.x, grab_.y - origin_.y};
    if (Box::of(Point{}, mirror_->picture_size()).contains(at)) hint = view.logical_point(at);
    lock_ = std::make_unique<LockedPointer>(globals_.pointer_constraints(), surface, pointer, hint);
    if (relative_ == nullptr) {
        relative_ = std::make_unique<RelativePointer>(globals_.relative_pointer(), pointer,
                                                      [this](PointF unaccelerated, PointF) { moved(unaccelerated); });
    }
    mirror_->hold_shortcuts_for_lock(true);
    RWIN_INFO("pointer", "asked the compositor to lock the pointer on {}", grab_.window);
}

void PointerGrabber::let_go() {
    if (lock_ == nullptr && confinement_ == nullptr) return;
    RWIN_INFO("pointer", "let go of the pointer on {}", grab_.window);
    lock_.reset();
    confinement_.reset();
    if (mirror_ != nullptr) mirror_->hold_shortcuts_for_lock(false);
    owed_x_ = 0.0;
    owed_y_ = 0.0;
}

// The compositor ended a hold of its own accord while the mirror still has
// the focus: its own unlock, which counts as the user's escape.
void PointerGrabber::notice_compositor_let_go() {
    const bool over = (lock_ != nullptr && lock_->over()) || (confinement_ != nullptr && confinement_->over());
    if (!over) return;
    let_go();
    if (mirror_ != nullptr && mirror_->surface() != nullptr && seat_.keyboard_focus() == mirror_->surface()) {
        escaped_ = true;
    }
}

void PointerGrabber::moved(PointF unaccelerated) {
    notice_compositor_let_go();
    if (lock_ == nullptr || !lock_->engaged() || escaped_) return;
    owed_x_ += unaccelerated.x;
    owed_y_ += unaccelerated.y;
    const int dx = static_cast<int>(std::trunc(owed_x_));
    const int dy = static_cast<int>(std::trunc(owed_y_));
    if (dx == 0 && dy == 0) return;
    owed_x_ -= dx;
    owed_y_ -= dy;
    link_.send_pointer_delta(grab_.window, dx, dy);
}

bool PointerGrabber::intercept(const KeyPress& press) {
    if (swallow_escape_release_ && !press.pressed && press.code == KEY_G) {
        swallow_escape_release_ = false;
        return true;
    }
    if (!wanted() || !press.pressed || press.code != KEY_G || !seat_.control_alt()) return false;
    RWIN_INFO("pointer", "escape pressed: the pointer is let go until the mirror is clicked");
    escaped_ = true;
    swallow_escape_release_ = true;
    let_go();
    return true;
}

bool PointerGrabber::intercept_press(const WindowMirror& mirror) {
    if (!escaped_ || !wanted() || &mirror != mirror_) return false;
    escaped_ = false;
    hold();
    return true;
}

bool PointerGrabber::locked_on(const WindowMirror& mirror) const {
    return &mirror == mirror_ && lock_ != nullptr && lock_->engaged() && !escaped_;
}

}  // namespace rwin::host
