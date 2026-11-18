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

void mouse_look(PointerGrabRule& rule, std::uint64_t& now, int moves, bool shown = false) {
    for (int i = 0; i < moves; ++i) {
        static_cast<void>(rule.grab_for(look(shown, std::nullopt, kMiddle, now)));
        rule.put_at({kMiddle.x + 10 + i, kMiddle.y - 3}, now);
        now += 8;
    }
}

PointerGrab free_moves(PointerGrabRule& rule, std::uint64_t& now, int moves, bool shown) {
    PointerGrab grab;
    for (int i = 0; i < moves; ++i) {
        const PointerGrabRule::Point to{kMiddle.x + 20 * (i + 1), kMiddle.y + 5 * (i + 1)};
        rule.put_at(to, now);
        now += PointerGrabRule::kSettleMs;
        grab = rule.grab_for(look(shown, std::nullopt, to, now));
    }
    return grab;
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
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
    check(!rule.warping(), "two returns are not yet a pattern");
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
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
    check(!rule.warping(), "a cursor found back at one point once a second is no mouse look");
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
    check(!rule.warping() && rule.grab_for(look(false, std::nullopt, {0, 0}, now)).kind == Kind::Lock,
          "a game that stopped putting the cursor back but keeps it hidden still reads the mouse");
    check(rule.grab_for(look(true, std::nullopt, {0, 0}, now)).kind == Kind::None,
          "and is let go once it shows its cursor - its menu is open");
}

void a_cursor_hidden_while_the_mouse_moves_locks() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
    const PointerGrab grab = free_moves(rule, now, 1, false);
    check(grab.kind == Kind::Lock && grab.x == kMiddle.x + 20 && grab.y == kMiddle.y + 5,
          "a cursor that stays hidden while the mouse moves it, without clip or warping, locks where it is");
    check(rule.looking_hidden() && !rule.warping(), "locked by the hidden cursor, not by warping");
    check(free_moves(rule, now, 4, false).kind == Kind::Lock, "relative motion that keeps it hidden keeps the lock");
}

void a_cursor_shown_again_on_movement_is_never_locked() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
    for (int i = 0; i < 6; ++i) {
        static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
        check(free_moves(rule, now, 1, true).kind == Kind::None,
              "a video player or an editor that hides the cursor shows it again when the mouse moves");
    }
    check(!rule.looking_hidden(), "and is never taken for mouse look");
}

void a_held_lock_keeps_its_point_through_clicks_and_edges() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
    const PointerGrab locked = free_moves(rule, now, 1, false);
    check(locked.kind == Kind::Lock, "locked by the hidden cursor");
    PointerGrabRule::Look pressed = look(false, std::nullopt, {2165, 309}, now + 1);
    pressed.button_held = true;
    const PointerGrab clicking = rule.grab_for(pressed);
    check(clicking == locked, "pressing a button while locked asks for the same lock, so the host keeps it");
    const PointerGrabRule::Point edge{2559, 0};
    for (int i = 0; i < PointerGrabRule::kReturnsToWarp; ++i) {
        static_cast<void>(rule.grab_for(look(false, std::nullopt, edge, now)));
        rule.nudged(edge, now);
        now += PointerGrabRule::kSettleMs;
    }
    check(rule.grab_for(look(false, std::nullopt, edge, now)) == locked,
          "a cursor stuck at the screen's edge under relative motion does not move the lock");
}

void moves_onto_a_hidden_cursor_are_the_mouse_s_steps() {
    PointerGrabRule rule;
    check(!rule.step_while_hidden({100, 100}, false, kMiddle).has_value(), "the first move has no step to take");
    const std::optional<PointerGrabRule::Point> step = rule.step_while_hidden({130, 90}, false, kMiddle);
    check(step.has_value() && step->x == 30 && step->y == -10,
          "a hidden cursor is moved by how far the mouse went, never to where it is");
    check(!rule.step_while_hidden({400, 400}, true, kMiddle).has_value(), "a shown cursor is moved to where the mouse is");
    const std::optional<PointerGrabRule::Point> after = rule.step_while_hidden({405, 401}, false, kMiddle);
    check(after.has_value() && after->x == 5 && after->y == 1,
          "hidden again, the step counts from the last place the mouse was seen");
}

void a_second_hidden_step_locks_at_once() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    static_cast<void>(rule.grab_for(look(true, std::nullopt, kMiddle, now)));
    static_cast<void>(rule.step_while_hidden({100, 100}, true, kMiddle));
    static_cast<void>(rule.step_while_hidden({140, 100}, false, kMiddle));
    check(rule.grab_for(look(false, std::nullopt, kMiddle, now)).kind == Kind::None,
          "one step onto a hidden cursor is not yet a lock: an editor hiding it while typing shows it on that move");
    static_cast<void>(rule.step_while_hidden({400, 100}, false, kMiddle));
    const PointerGrab grab = rule.grab_for(look(false, std::nullopt, kMiddle, now));
    check(grab.kind == Kind::Lock && grab.x == kMiddle.x && grab.y == kMiddle.y,
          "the second step locks with no wait, where the game keeps its cursor, so a fast flick cannot leave");
    static_cast<void>(rule.step_while_hidden({100, 100}, true, kMiddle));
    static_cast<void>(rule.grab_for(look(true, std::nullopt, kMiddle, now)));
    static_cast<void>(rule.step_while_hidden({140, 100}, false, kMiddle));
    check(rule.grab_for(look(false, std::nullopt, kMiddle, now)).kind == Kind::None,
          "after the cursor shows, the count starts again");
}

void a_lock_on_a_hidden_cursor_ends_when_it_shows() {
    PointerGrabRule rule;
    std::uint64_t now = 0;
    static_cast<void>(rule.grab_for(look(false, std::nullopt, kMiddle, now)));
    check(free_moves(rule, now, 1, false).kind == Kind::Lock, "locked");
    check(free_moves(rule, now, 1, true).kind == Kind::None, "the game shows its cursor: let go");
    check(rule.grab_for(look(false, std::nullopt, kMiddle, now)).kind == Kind::None,
          "hidden again, it is not locked before the mouse moves it");
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

}

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
    a_cursor_hidden_while_the_mouse_moves_locks();
    a_cursor_shown_again_on_movement_is_never_locked();
    a_lock_on_a_hidden_cursor_ends_when_it_shows();
    moves_onto_a_hidden_cursor_are_the_mouse_s_steps();
    a_second_hidden_step_locks_at_once();
    a_held_lock_keeps_its_point_through_clicks_and_edges();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
