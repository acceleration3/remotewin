#pragma once

#include <cstdint>
#include <optional>
#include <utility>

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

    // The screen point the cursor was put at; nothing for a window that
    // is gone.
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> move_to(
        const WindowSurface& surface, std::int32_t frame_x, std::int32_t frame_y) const;
    void press(const WindowSurface& surface, std::int32_t frame_x, std::int32_t frame_y,
               Button button, bool pressed) const;
    // Where the cursor already is: a locked pointer's button, which acts
    // wherever the application holds its cursor.
    void press_in_place(const WindowSurface& surface, Button button, bool pressed) const;
    // At a point of the screen rather than of a window: a mirror of the
    // whole monitor, which takes no window's focus for the press - the
    // press itself activates whatever it lands on, as a real one does.
    static void move_on_screen(std::int32_t screen_x, std::int32_t screen_y);
    static void press_on_screen(std::int32_t screen_x, std::int32_t screen_y, Button button,
                                bool pressed);
    // Moved by an amount, as a mouse moves it (relative SendInput): the
    // amount reaches raw input as it is, and the cursor moves by it as
    // Windows' pointer speed and acceleration have it.
    static void nudge(std::int32_t dx, std::int32_t dy);

private:
    void take_focus_for(const WindowSurface& surface, bool pressed) const;

    const WindowFocus& focus_;
};

}  // namespace rwin::guest
