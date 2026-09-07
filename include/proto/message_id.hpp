#pragma once

#include <cstdint>

namespace rwin {

// Host ids below 64, guest ids at 64 and above, so a message decoded on
// the wrong side is obviously wrong rather than plausibly right.
enum class MessageId : std::uint8_t {
    ListWindows = 1,
    Subscribe = 2,
    Unsubscribe = 3,
    PointerMove = 4,
    PointerButton = 5,
    Key = 6,
    CloseWindow = 7,
    ResizeWindow = 8,
    FocusWindow = 9,

    WindowList = 64,
    Frame = 65,
    WindowGone = 66,
    // What part of itself the window says the pointer is over. Pushed
    // when it changes rather than asked on each click, so a press can be
    // acted on at once instead of after a round trip.
    NonClientHit = 67,
    CursorShape = 68,
    CursorChanged = 69,
};

}  // namespace rwin
