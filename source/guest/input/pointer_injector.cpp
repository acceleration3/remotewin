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

}  // namespace

void PointerInjector::move_to(const WindowSurface& surface, std::int32_t frame_x,
                              std::int32_t frame_y) const {
    if (!surface.valid()) return;
    move_cursor(surface.screen_x(frame_x), surface.screen_y(frame_y));
}

void PointerInjector::press(const WindowSurface& surface, std::int32_t frame_x,
                            std::int32_t frame_y, Button button, bool pressed) const {
    if (!surface.valid()) return;

    // Only for buttons. A move that stole the foreground would dismiss a
    // dropdown the pointer was merely passing over.
    focus_.give_to(
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface.handle())));

    const std::int32_t x = surface.screen_x(frame_x);
    const std::int32_t y = surface.screen_y(frame_y);
    move_cursor(x, y);

    DWORD flags = 0;
    switch (button) {
        case Button::Left:
            flags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case Button::Middle:
            flags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case Button::Right:
            flags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
    }
    send_button(flags);
    RWIN_DEBUG("input", "button {} {} at screen {},{}", static_cast<int>(button),
               pressed ? "down" : "up", x, y);
}

}  // namespace rwin::guest
