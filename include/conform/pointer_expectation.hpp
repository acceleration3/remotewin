#pragma once

#include "conform/expectation.hpp"
#include "host/mirror/mirror_state.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/probe/cursor_truth.hpp"
#include "proto/probe/window_truth.hpp"

#include <QString>

#include <optional>
#include <vector>

namespace rwin::conform {

// What Windows says of the cursor and the pointer, turned into what the
// mirror has to do - one row per behaviour, as Expectation does for a
// window, and read off the truth directly rather than through the guest
// half's PointerGrabRule, which is what decides on the other side.
//
// The cursor rows are of the mirror the pointer is over (the scenario's last
// pointer step names it); the hold rows of the foreground window's mirror,
// whose pointer the application holds.
class PointerExpectation {
public:
    struct Sides {
        const CursorTruth& cursor;
        // The window the pointer is over, and the foreground one among the
        // scenario's (nothing when none of them is).
        const WindowTruth& pointer_window;
        const WindowTruth* foreground;
        const host::MirrorState& pointer_mirror;
        const host::MirrorState* foreground_mirror;
        // What the last stage wanted held: a stage that wants nothing after
        // one that did is a release.
        PointerGrab::Kind held_before = PointerGrab::Kind::None;
        // Why the user on this desktop had the pointer let go - the mirror
        // lost the focus, the escape was pressed - or empty: nothing is to
        // be held then, whatever the guest wants.
        QString let_go{};
        int tolerance = 0;
    };

    // Clipped no larger than this, the cursor cannot be moved within its
    // clip by anything absolute (PointerGrabRule::kSmallClip).
    static constexpr int kTinyClip = 16;
    // Moves put back in a row before a cursor is seen to be held for mouse
    // look (PointerGrabRule::kReturnsToWarp).
    static constexpr std::uint32_t kPutBackToLook = 3;

    explicit PointerExpectation(Sides sides) : sides_(sides) {}

    // What the application wants of the pointer, by Windows' own account:
    // a hidden cursor clipped, put back on every move, or read raw is mouse
    // look; a visible one clipped is confined.
    [[nodiscard]] PointerGrab::Kind wanted() const;

    [[nodiscard]] std::vector<Expectation::Check> checks() const;

private:
    Sides sides_;
};

}  // namespace rwin::conform
