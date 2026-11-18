#include "proto/pointer_grab_rule.hpp"

#include <cstdlib>

namespace rwin {

namespace {
bool near(PointerGrabRule::Point a, PointerGrabRule::Point b) noexcept {
    return std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1;
}

bool far(PointerGrabRule::Point a, PointerGrabRule::Point b) noexcept {
    return std::abs(a.x - b.x) > PointerGrabRule::kWarpDistance || std::abs(a.y - b.y) > PointerGrabRule::kWarpDistance;
}

}

void PointerGrabRule::put_at(Point at, std::uint64_t now_ms) noexcept {
    pending_ = Pending{at, true, now_ms};
}

void PointerGrabRule::nudged(Point from, std::uint64_t now_ms) noexcept {
    pending_ = Pending{from, false, now_ms};
}

void PointerGrabRule::forget() noexcept {
    pending_.reset();
    returns_ = 0;
    free_moves_ = 0;
    warping_ = false;
    held_at_.reset();
    dragged_away_ = false;
    hidden_at_.reset();
    locked_at_.reset();
    last_target_.reset();
    hidden_steps_ = 0;
}

std::optional<PointerGrabRule::Point> PointerGrabRule::step_while_hidden(Point target, bool cursor_shown,
                                                                         Point cursor) noexcept {
    const std::optional<Point> from = last_target_;
    last_target_ = target;
    if (cursor_shown) hidden_steps_ = 0;
    if (cursor_shown || !from.has_value()) return std::nullopt;
    if (++hidden_steps_ >= kHiddenStepsToLock && !hidden_at_.has_value()) hidden_at_ = cursor;
    return Point{target.x - from->x, target.y - from->y};
}

void PointerGrabRule::note_return(Point at, std::uint64_t now_ms) noexcept {
    const bool again = returns_ > 0 && near(at, return_point_) && now_ms - last_return_ms_ <= kReturnGapMs;
    returns_ = again ? returns_ + 1 : 1;
    if (!again) return_point_ = at;
    last_return_ms_ = now_ms;
    free_moves_ = 0;
    if (returns_ >= kReturnsToWarp) warping_ = true;
}

void PointerGrabRule::note_hidden(const Look& look) noexcept {
    if (look.cursor_shown || hidden_at_.has_value()) return;
    hidden_at_ = look.cursor;
}

void PointerGrabRule::note_free() noexcept {
    if (++free_moves_ < kFreeMovesToStop) return;
    warping_ = false;
    returns_ = 0;
}

void PointerGrabRule::judge(const Look& look) noexcept {
    if (!pending_.has_value()) return;
    const bool settled = look.now_ms - pending_->since_ms >= kSettleMs;
    const bool at_point = near(look.cursor, pending_->point);
    if (pending_->absolute && !at_point) {
        if (look.button_held && far(look.cursor, pending_->point)) dragged_away_ = true;
        note_hidden(look);
        note_return(look.cursor, look.now_ms);
        pending_.reset();
        return;
    }
    if (!settled) return;
    note_hidden(look);
    if (pending_->absolute || !at_point) {
        note_free();
    } else {
        note_return(look.cursor, look.now_ms);
    }
    pending_.reset();
}

PointerGrab PointerGrabRule::grab_for(const Look& look) noexcept {
    if (look.foreground != window_) {
        forget();
        window_ = look.foreground;
    }
    if (!look.button_held) dragged_away_ = false;
    if (look.cursor_shown) {
        hidden_at_.reset();
        hidden_steps_ = 0;
    }
    judge(look);
    PointerGrab grab;
    if (look.foreground.none()) return grab;
    grab.window = look.foreground;
    const bool small_clip = look.clip.has_value() && look.clip->width() <= kSmallClip
                            && look.clip->height() <= kSmallClip;
    const bool hidden = !look.cursor_shown;
    const bool held = (hidden && look.button_held) || dragged_away_;
    if (!held) {
        held_at_.reset();
    } else if (!held_at_.has_value()) {
        held_at_ = look.cursor;
    }
    const bool lock =
        (hidden && (look.clip.has_value() || warping_ || hidden_at_.has_value())) || held || small_clip;
    if (look.clip.has_value()) grab.area = *look.clip;
    if (!lock) locked_at_.reset();
    if (lock) {
        grab.kind = PointerGrab::Kind::Lock;
        const Point found = warping_ ? return_point_
                         : look.clip.has_value()
                             ? Point{(look.clip->left + look.clip->right) / 2, (look.clip->top + look.clip->bottom) / 2}
                         : held_at_.has_value() ? *held_at_
                         : hidden_at_.has_value() ? *hidden_at_
                                                  : look.cursor;
        if (!locked_at_.has_value()) locked_at_ = found;
        const Point at = *locked_at_;
        grab.x = at.x;
        grab.y = at.y;
        return grab;
    }
    if (look.clip.has_value()) grab.kind = PointerGrab::Kind::Confine;
    return grab;
}

}
