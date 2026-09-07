#pragma once

#include <cstdint>

namespace rwin::guest {

class WindowFocus;
class WindowSurface;

// Mouse movement and buttons, delivered where the guest will believe them.
//
// A real absolute move at the desktop, and then a button event with no
// position of its own — so Windows does its own hit testing and routing.
// Posting a WM_LBUTTONDOWN to a window instead means deciding which
// window and which coordinate space, and being wrong for anything with
// custom chrome, raw input, or a toolkit that reads the input queue
// rather than its message loop. Windows 11's own Notepad is one of those.
class PointerInjector {
public:
    enum class Button : std::uint8_t { Left = 0, Middle = 1, Right = 2 };

    explicit PointerInjector(const WindowFocus& focus) noexcept : focus_(focus) {}

    void move_to(const WindowSurface& surface, std::int32_t frame_x,
                 std::int32_t frame_y) const;
    void press(const WindowSurface& surface, std::int32_t frame_x, std::int32_t frame_y,
               Button button, bool pressed) const;

private:
    const WindowFocus& focus_;
};

}  // namespace rwin::guest
