#pragma once

#include <cstdint>

namespace rwin::guest {

// The window pointer input is meant for, made the one it lands on.
//
// The host sends a point of the mirror the pointer is over, which is the
// same point of that mirror's window (docs/review-and-redesign.md R2); the
// input itself goes in at a point of the guest's screen, and Windows hands
// it to whatever is topmost there. Where the host's compositor has the
// windows overlapping otherwise than they do here, that is another window -
// so the one meant is raised first, without activating it (SetWindowPos
// HWND_TOP, SWP_NOACTIVATE). Nobody sees the guest's screen, so its
// stacking is free to follow the pointer.
//
// Not while a button the host pressed is held: the window that took the
// press holds the capture and gets the rest wherever it goes.
class InputTarget {
public:
    // Before input at (x, y) of the screen for ``window``. ``now`` waits for
    // the raise, which a press needs before it lands; motion leaves it to
    // the window's thread.
    void raise(void* window, std::int32_t x, std::int32_t y, bool now);
    void pressed(bool down) noexcept;

private:
    int held_ = 0;
};

}  // namespace rwin::guest
