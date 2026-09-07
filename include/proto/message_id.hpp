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
    // Windows started moving a window, and the guest stopped it. The
    // host owns the drag from here.
    DragStarted = 67,
    CursorShape = 68,
    CursorChanged = 69,
};

}  // namespace rwin
