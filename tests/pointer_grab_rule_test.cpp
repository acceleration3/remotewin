#include "proto/pointer_grab_rule.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

using rwin::PointerGrab;
using rwin::PointerGrabRule;
using Kind = PointerGrab::Kind;

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

constexpr rwin::WindowId kGame{0x500c4, 1};
constexpr PointerGrabRule::Point kMiddle{1280, 720};
constexpr PointerGrab::Area kWindowClip{400, 300, 1200, 900};

PointerGrabRule::Look look(bool shown, std::optional<PointerGrab::Area> clip, PointerGrabRule::Point cursor,
                           std::uint64_t now, rwin::WindowId foreground = kGame) {
    return PointerGrabRule::Look{foreground, shown, clip, cursor, now};
}

// SetCursorPos to the middle on every WM_MOUSEMOVE: each absolute move the
// pointer makes is found back at the middle when next looked at.
void mouse_look(PointerGrabRule& rule, std::uint64_t& now, int moves, bool shown = false) {
    for (int i = 0; i < moves; ++i) {
        static_cast<void>(rule.grab_for(look(shown, std::nullopt, kMiddle, now)));
        rule.put_at({kMiddle.x + 10 + i, kMiddle.y - 3}, now);
        now += 8;
    }
}

void a_hidden_cursor_alone_is_left_alone() {
    PointerGrabRule rule;
    const PointerGrab grab = rule.grab_for(look(false, std::nullopt, kMiddle, 0));
    check(grab.kind == Kind::None, "a video player hiding its cursor is not a lock");
    check(grab.window == kGame, "but the grab still names the foreground window");
}

void a_clipped_visible_cursor_is_confined() {
    PointerGrabRule rule;
    const PointerGrab grab = rule.grab_for(look(true, kWindowClip, {500, 500}, 0));
    check(grab.kind == Kind::Confine && grab.area == kWindowClip, "a strategy game's clip confines to the clip");
}

void a_hidden_clipped_cursor_is_locked_at_the_clip_middle() {
    PointerGrabRule rule;
    const PointerGrab grab = rule.grab_for(look(false, kWindowClip, {500, 500}, 0));
    check(grab.kind == Kind::Lock, "a raw input game's hidden, clipped cursor locks");
    check(grab.x == 800 && grab.y == 600 && grab.area == kWindowClip, "held at the clip's middle");
}

void a_tiny_clip_locks_even_shown() {
    PointerGrabRule rule;
    const PointerGrab grab = rule.grab_for(look(true, PointerGrab::Area{640, 360, 641, 361}, {640, 360}, 0));
    check(grab.kind == Kind::Lock, "a one-pixel clip cannot be moved within by absolute motion");
}

void mouse_look_without_a_clip_locks_after_three_returns() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    mouse_look(rule, now, 2);
    check(rule.grab_for(look(false, std::nullopt, kMiddle, now)).kind == Kind::None, "two returns are not yet a pattern");
    rule.put_at({kMiddle.x + 40, kMiddle.y}, now);
    now += 8;
    const PointerGrab grab = rule.grab_for(look(false, std::nullopt, kMiddle, now));
    check(grab.kind == Kind::Lock && grab.x == kMiddle.x && grab.y == kMiddle.y,
          "the third return locks, at the point the cursor is put back to");
}

void returns_to_a_visible_cursor_are_not_a_lock() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    mouse_look(rule, now, 5, true);
    check(rule.grab_for(look(true, std::nullopt, kMiddle, now)).kind == Kind::None,
          "a shown cursor that is put back is somebody's drag, not mouse look");
    check(rule.warping(), "though it is seen to be put back");
}

void returns_too_far_apart_do_not_add_up() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    for (int i = 0; i < 5; ++i) {
        static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
        rule.put_at({kMiddle.x + 10, kMiddle.y}, now);
        now += PointerGrabRule::kReturnGapMs + 1;
    }
    check(rule.grab_for(look(false, std::nullopt, kMiddle, now)).kind == Kind::None,
          "a cursor found back at one point once a second is no mouse look");
}

void a_move_judged_too_soon_counts_for_nothing() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    mouse_look(rule, now, PointerGrabRule::kReturnsToWarp + 1);
    rule.put_at({kMiddle.x + 10, kMiddle.y}, now);
    check(rule.grab_for(look(false, std::nullopt, {kMiddle.x + 10, kMiddle.y}, now + 1)).kind == Kind::Lock,
          "a cursor still where it was put a millisecond ago has not been left there yet");
}

void mouse_look_ends_when_moves_stay() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    mouse_look(rule, now, 3);
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
    check(rule.warping(), "locked by mouse look");
    for (int i = 0; i < PointerGrabRule::kFreeMovesToStop; ++i) {
        const PointerGrabRule::Point to{kMiddle.x + 100 + i * 10, kMiddle.y};
        rule.put_at(to, now);
        now += PointerGrabRule::kSettleMs;
        static_cast<void>(rule.grab_for(look(false, std::nullopt, to, now)));
    }
    check(!rule.warping() && rule.grab_for(look(false, std::nullopt, {0, 0}, now)).kind == Kind::None,
          "a game that stopped putting the cursor back - its menu is open - is let go");
}

void nudges_put_back_keep_the_lock() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    for (int i = 0; i < PointerGrabRule::kReturnsToWarp; ++i) {
        static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
        rule.nudged(kMiddle, now);
        now += PointerGrabRule::kSettleMs;
    }
    check(rule.grab_for(look(false, std::nullopt, kMiddle, now)).kind == Kind::Lock,
          "relative moves found back at the middle are mouse look too");
}

void losing_the_foreground_lets_go() {
    PointerGrabRule rule;
    check(rule.grab_for(look(false, kWindowClip, kMiddle, 0)).kind == Kind::Lock, "locked");
    const PointerGrab grab = rule.grab_for(look(false, kWindowClip, kMiddle, 10, rwin::WindowId{}));
    check(grab.kind == Kind::None && grab.window.none(), "another application's window in front: nothing is held");
    std::uint64_t now = 20;
    mouse_look(rule, now, 3);
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now, rwin::WindowId{0x700, 2})));
    check(!rule.warping(), "what was seen of one window is forgotten when another is in front");
}

}  // namespace

int main() {
    a_hidden_cursor_alone_is_left_alone();
    a_clipped_visible_cursor_is_confined();
    a_hidden_clipped_cursor_is_locked_at_the_clip_middle();
    a_tiny_clip_locks_even_shown();
    mouse_look_without_a_clip_locks_after_three_returns();
    returns_to_a_visible_cursor_are_not_a_lock();
    returns_too_far_apart_do_not_add_up();
    a_move_judged_too_soon_counts_for_nothing();
    mouse_look_ends_when_moves_stay();
    nudges_put_back_keep_the_lock();
    losing_the_foreground_lets_go();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
