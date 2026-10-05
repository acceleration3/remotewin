#include "guest/input/pointer_injector.hpp"

#include "guest/windows/window_focus.hpp"
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
// move above just settled.
void send_button(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    ::SendInput(1, &input, sizeof(input));
}

DWORD button_flags(PointerInjector::Button button, bool pressed) {
    switch (button) {
        case PointerInjector::Button::Left:
            return pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        case PointerInjector::Button::Middle:
            return pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        case PointerInjector::Button::Right:
            return pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    }
    return 0;
}

}  // namespace

std::optional<std::pair<std::int32_t, std::int32_t>> PointerInjector::move_to(
    const WindowSurface& surface, std::int32_t frame_x, std::int32_t frame_y) const {
    if (!surface.valid()) return std::nullopt;
    const std::int32_t x = surface.screen_x(frame_x);
    const std::int32_t y = surface.screen_y(frame_y);
    move_cursor(x, y);
    return std::pair{x, y};
}

// Only for a press. A move that stole the foreground would dismiss a
// dropdown the pointer was merely passing over; and the release comes
// after the press has activated whatever it landed on - an owned dialog of
// this window - which taking the foreground back would undo.
void PointerInjector::take_focus_for(const WindowSurface& surface, bool pressed) const {
    if (!pressed) return;
    focus_.give_to(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface.handle())));
}

void PointerInjector::press(const WindowSurface& surface, std::int32_t frame_x,
                            std::int32_t frame_y, Button button, bool pressed) const {
    if (!surface.valid()) return;
    take_focus_for(surface, pressed);
    const std::int32_t x = surface.screen_x(frame_x);
    const std::int32_t y = surface.screen_y(frame_y);
    move_cursor(x, y);
    send_button(button_flags(button, pressed));
    RWIN_DEBUG("input", "button {} {} at screen {},{}", static_cast<int>(button),
               pressed ? "down" : "up", x, y);
}

void PointerInjector::press_in_place(const WindowSurface& surface, Button button, bool pressed) const {
    if (!surface.valid()) return;
    take_focus_for(surface, pressed);
    send_button(button_flags(button, pressed));
    RWIN_DEBUG("input", "button {} {} where the cursor is held", static_cast<int>(button), pressed ? "down" : "up");
}

void PointerInjector::turn(const WindowSurface& surface, std::int32_t frame_x, std::int32_t frame_y,
                           std::int32_t dx, std::int32_t dy) const {
    if (!surface.valid()) return;
    turn_on_screen(surface.screen_x(frame_x), surface.screen_y(frame_y), dx, dy);
}

void PointerInjector::turn_on_screen(std::int32_t screen_x, std::int32_t screen_y, std::int32_t dx,
                                     std::int32_t dy) {
    move_cursor(screen_x, screen_y);
    turn_in_place(dx, dy);
}

void PointerInjector::turn_in_place(std::int32_t dx, std::int32_t dy) {
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

void PointerInjector::move_on_screen(std::int32_t screen_x, std::int32_t screen_y) {
    move_cursor(screen_x, screen_y);
}

void PointerInjector::press_on_screen(std::int32_t screen_x, std::int32_t screen_y, Button button,
                                      bool pressed) {
    move_cursor(screen_x, screen_y);
    send_button(button_flags(button, pressed));
    RWIN_DEBUG("input", "button {} {} at screen {},{}", static_cast<int>(button),
               pressed ? "down" : "up", screen_x, screen_y);
}

// Without MOUSEEVENTF_MOVE_NOCOALESCE, deliberately: raw input gets every
// move apart either way, and coalescing only merges WM_MOUSEMOVEs still
// queued. A game that puts its cursor back on each WM_MOUSEMOVE reads each
// one against the point it put it at; a second message queued before that
// carries a position taken before the put, and would count the first move
// twice. Merged, the moves are counted once, as with a real mouse.
void PointerInjector::nudge(std::int32_t dx, std::int32_t dy) {
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
