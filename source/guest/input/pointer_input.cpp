#include "guest/input/pointer_input.hpp"

#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

// Absolute mouse input is expressed over a 0..65535 grid spanning the
// screen, not in pixels.
constexpr long long kAbsoluteRange = 65535;

void move_cursor(std::int32_t screen_x, std::int32_t screen_y) {
    const int width = ::GetSystemMetrics(SM_CXSCREEN);
    const int height = ::GetSystemMetrics(SM_CYSCREEN);
    if (width <= 1 || height <= 1) {
        ::SetCursorPos(screen_x, screen_y);
        return;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.mi.dx = static_cast<LONG>(screen_x * kAbsoluteRange / (width - 1));
    input.mi.dy = static_cast<LONG>(screen_y * kAbsoluteRange / (height - 1));
    ::SendInput(1, &input, sizeof(input));
}

// No MOVE flag: the button acts wherever the cursor already is, which the
// move before it just settled.
void send_button(PointerButton::Button button, bool pressed) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    switch (button) {
        case PointerButton::Button::Left:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case PointerButton::Button::Middle:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case PointerButton::Button::Right:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case PointerButton::Button::Back:
        case PointerButton::Button::Forward:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = button == PointerButton::Button::Back ? XBUTTON1 : XBUTTON2;
            break;
    }
    ::SendInput(1, &input, sizeof(input));
}

}  // namespace

std::optional<std::pair<std::int32_t, std::int32_t>> move_pointer_to(const WindowSurface& surface, std::int32_t x,
                                                                     std::int32_t y) {
    if (!surface.valid()) return std::nullopt;
    const std::int32_t screen_x = surface.screen_x(x);
    const std::int32_t screen_y = surface.screen_y(y);
    move_cursor(screen_x, screen_y);
    return std::pair{screen_x, screen_y};
}

void press_pointer(const WindowSurface& surface, std::int32_t x, std::int32_t y, PointerButton::Button button,
                   bool pressed) {
    if (!surface.valid()) return;
    press_pointer_on_screen(surface.screen_x(x), surface.screen_y(y), button, pressed);
}

void press_pointer_in_place(PointerButton::Button button, bool pressed) {
    send_button(button, pressed);
    RWIN_DEBUG("input", "button {} {} where the cursor is held", static_cast<int>(button), pressed ? "down" : "up");
}

void move_pointer_on_screen(std::int32_t screen_x, std::int32_t screen_y) { move_cursor(screen_x, screen_y); }

void press_pointer_on_screen(std::int32_t screen_x, std::int32_t screen_y, PointerButton::Button button,
                             bool pressed) {
    move_cursor(screen_x, screen_y);
    send_button(button, pressed);
    RWIN_DEBUG("input", "button {} {} at screen {},{}", static_cast<int>(button), pressed ? "down" : "up", screen_x,
               screen_y);
}

void turn_wheel(const WindowSurface& surface, std::int32_t x, std::int32_t y, std::int32_t dx, std::int32_t dy) {
    if (!surface.valid()) return;
    turn_wheel_on_screen(surface.screen_x(x), surface.screen_y(y), dx, dy);
}

void turn_wheel_on_screen(std::int32_t screen_x, std::int32_t screen_y, std::int32_t dx, std::int32_t dy) {
    move_cursor(screen_x, screen_y);
    turn_wheel_in_place(dx, dy);
}

void turn_wheel_in_place(std::int32_t dx, std::int32_t dy) {
    INPUT inputs[2] = {};
    UINT count = 0;
    if (dy != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[count].mi.mouseData = static_cast<DWORD>(dy);
        ++count;
    }
    if (dx != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        inputs[count].mi.mouseData = static_cast<DWORD>(dx);
        ++count;
    }
    if (count == 0) return;
    if (::SendInput(count, inputs, sizeof(INPUT)) != count) {
        RWIN_WARN("input", "wheel {},{} was not injected (error {})", dx, dy, ::GetLastError());
        return;
    }
    RWIN_TRACE("input", "wheel {},{}", dx, dy);
}

// Without MOUSEEVENTF_MOVE_NOCOALESCE, deliberately: raw input gets every
// move apart either way, and coalescing only merges WM_MOUSEMOVEs still
// queued. A game that puts its cursor back on each WM_MOUSEMOVE reads each
// one against the point it put it at; a second message queued before that
// carries a position taken before the put, and would count the first move
// twice. Merged, the moves are counted once, as with a real mouse.
void nudge_pointer(std::int32_t dx, std::int32_t dy) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    if (::SendInput(1, &input, sizeof(input)) != 1) {
        RWIN_WARN("input", "relative move {},{} refused: error {}", dx, dy, ::GetLastError());
    }
}

}  // namespace rwin::guest
