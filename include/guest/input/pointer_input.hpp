#pragma once

#include "proto/pointer_button.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace rwin::guest {

class WindowSurface;

// Mouse movement, buttons and wheels, delivered where the guest will
// believe them.
//
// A real absolute move at the desktop, and then a button event with no
// position of its own - so Windows does its own hit testing and routing.
// Posting a WM_LBUTTONDOWN to a window instead means deciding which window
// and which coordinate space, and being wrong for anything with custom
// chrome, raw input, or a toolkit that reads the input queue rather than
// its message loop. Windows 11's own Notepad is one of those.
//
// A press activates the window it lands on by itself, as a real one does
// (RAIL's rule): nothing here moves the foreground.

// The screen point the cursor was put at; nothing for a window that is
// gone. (x, y) are in the window's frame.
[[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> move_pointer_to(const WindowSurface& surface,
                                                                                   std::int32_t x, std::int32_t y);
void press_pointer(const WindowSurface& surface, std::int32_t x, std::int32_t y, PointerButton::Button button,
                   bool pressed);
// Where the cursor already is: a locked pointer's button, which acts
// wherever the application holds its cursor.
void press_pointer_in_place(PointerButton::Button button, bool pressed);
// At a point of the screen rather than of a window: a mirror of the whole
// monitor, or a button held while Windows moves the window under it.
void move_pointer_on_screen(std::int32_t screen_x, std::int32_t screen_y);
void press_pointer_on_screen(std::int32_t screen_x, std::int32_t screen_y, PointerButton::Button button,
                             bool pressed);
// A wheel turned by ``dx``, ``dy`` in WHEEL_DELTA units: Windows sends the
// wheel to the window under the cursor, so the cursor is put there first.
void turn_wheel(const WindowSurface& surface, std::int32_t x, std::int32_t y, std::int32_t dx, std::int32_t dy);
void turn_wheel_on_screen(std::int32_t screen_x, std::int32_t screen_y, std::int32_t dx, std::int32_t dy);
void turn_wheel_in_place(std::int32_t dx, std::int32_t dy);
// Moved by an amount, as a mouse moves it (relative SendInput): the amount
// reaches raw input as it is, and the cursor moves by it as Windows'
// pointer speed and acceleration have it.
void nudge_pointer(std::int32_t dx, std::int32_t dy);

}  // namespace rwin::guest
