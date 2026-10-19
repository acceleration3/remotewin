#pragma once

#include "proto/pointer_grab.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

// Whether the guest application wants the pointer confined or locked,
// judged from what any process can see of it - no process can read
// another's raw input registration, so what a game asked for is inferred
// from what it does to the cursor:
//
//   lock     the foreground window is mirrored, and either
//            - the cursor is hidden and clipped (ClipCursor), or
//            - the cursor is hidden and keeps being put back at one point
//              after the pointer moves it: at least kReturnsToWarp times in
//              a row, each within kReturnGapMs of the last (the classic
//              mouse look: SetCursorPos to the middle on every move), or
//            - the clip is at most kSmallClip square, hidden or not:
//              nothing absolute can move a cursor held that tightly.
//   confine  the foreground window is mirrored, the cursor is clipped, and
//            it is not a lock.
//   none     otherwise - a cursor that is only hidden, as a video player
//            hides it, is left alone.
//
// Shared by both ends because the guest decides by it and the host's tests
// state it; it is the definition, not an estimate of one.
class PointerGrabRule {
public:
    struct Point {
        std::int32_t x = 0;
        std::int32_t y = 0;
        [[nodiscard]] bool operator==(const Point&) const noexcept = default;
    };

    // One look at the guest.
    struct Look {
        // The foreground window when it is one the host shows, else none.
        WindowId foreground;
        // CURSOR_SHOWING with a cursor to show.
        bool cursor_shown = true;
        // GetClipCursor, when it is less than the whole desktop.
        std::optional<PointerGrab::Area> clip;
        Point cursor;
        std::uint64_t now_ms = 0;
    };

    static constexpr std::int32_t kSmallClip = 16;
    static constexpr int kReturnsToWarp = 3;
    static constexpr std::uint64_t kReturnGapMs = 1000;
    // A move is taken to have stayed where it went only this long after it
    // was made: an application answers its WM_MOUSEMOVE in its own time.
    static constexpr std::uint64_t kSettleMs = 25;
    static constexpr int kFreeMovesToStop = 2;

    // The host's pointer was put at ``at``, a guest screen point.
    void put_at(Point at, std::uint64_t now_ms) noexcept;
    // The host's pointer moved the cursor by an amount, from ``from``.
    void nudged(Point from, std::uint64_t now_ms) noexcept;

    // What the application wants, as of ``look``. Each look also weighs
    // what became of the last move: put back, or left where it went.
    [[nodiscard]] PointerGrab grab_for(const Look& look) noexcept;

    // Whether the cursor is being put back at one point, as last judged.
    [[nodiscard]] bool warping() const noexcept { return warping_; }

private:
    struct Pending {
        Point point;
        bool absolute = false;
        std::uint64_t since_ms = 0;
    };

    void judge(const Look& look) noexcept;
    void note_return(Point at, std::uint64_t now_ms) noexcept;
    void note_free() noexcept;
    void forget() noexcept;

    std::optional<Pending> pending_;
    WindowId window_;
    Point return_point_;
    int returns_ = 0;
    std::uint64_t last_return_ms_ = 0;
    int free_moves_ = 0;
    bool warping_ = false;
};

}  // namespace rwin
