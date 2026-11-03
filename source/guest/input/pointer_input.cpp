#include "guest/input/pointer_input.hpp"

#include "guest/input/input_batch.hpp"
#include "guest/input/input_port.hpp"
#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {
constexpr long long kAbsoluteRange = 65535;

void add_move(InputBatch& batch, std::int32_t screen_x, std::int32_t screen_y) {
    const int width = ::GetSystemMetrics(SM_CXSCREEN);
    const int height = ::GetSystemMetrics(SM_CYSCREEN);
    if (width <= 1 || height <= 1) {
        ::SetCursorPos(screen_x, screen_y);
        return;
    }
    batch.add_mouse(static_cast<LONG>(screen_x * kAbsoluteRange / (width - 1)),
                    static_cast<LONG>(screen_y * kAbsoluteRange / (height - 1)), 0,
                    MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE);
}

void move_cursor(std::int32_t screen_x, std::int32_t screen_y) {
    InputBatch batch;
    add_move(batch, screen_x, screen_y);
    if (!batch.empty()) InputPort::shared().send(batch);
}

void add_button(InputBatch& batch, PointerButton::Button button, bool pressed) {
    DWORD flags = 0;
    DWORD data = 0;
    switch (button) {
        case PointerButton::Button::Left:
            flags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case PointerButton::Button::Middle:
            flags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case PointerButton::Button::Right:
            flags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case PointerButton::Button::Back:
        case PointerButton::Button::Forward:
            flags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            data = button == PointerButton::Button::Back ? XBUTTON1 : XBUTTON2;
            break;
    }
    batch.add_mouse(0, 0, data, flags);
}

void send_button(PointerButton::Button button, bool pressed) {
    InputBatch batch;
    add_button(batch, button, pressed);
    InputPort::shared().send(batch);
}

void add_wheel(InputBatch& batch, std::int32_t dx, std::int32_t dy) {
    if (dy != 0) batch.add_mouse(0, 0, static_cast<DWORD>(dy), MOUSEEVENTF_WHEEL);
    if (dx != 0) batch.add_mouse(0, 0, static_cast<DWORD>(dx), MOUSEEVENTF_HWHEEL);
}

}

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
    InputBatch batch;
    add_move(batch, screen_x, screen_y);
    add_button(batch, button, pressed);
    InputPort::shared().send(batch);
    RWIN_DEBUG("input", "button {} {} at screen {},{}", static_cast<int>(button), pressed ? "down" : "up", screen_x,
               screen_y);
}

void turn_wheel(const WindowSurface& surface, std::int32_t x, std::int32_t y, std::int32_t dx, std::int32_t dy) {
    if (!surface.valid()) return;
    turn_wheel_on_screen(surface.screen_x(x), surface.screen_y(y), dx, dy);
}

void turn_wheel_on_screen(std::int32_t screen_x, std::int32_t screen_y, std::int32_t dx, std::int32_t dy) {
    InputBatch batch;
    add_move(batch, screen_x, screen_y);
    add_wheel(batch, dx, dy);
    if (!batch.empty()) InputPort::shared().send(batch);
    RWIN_TRACE("input", "wheel {},{} at screen {},{}", dx, dy, screen_x, screen_y);
}

void turn_wheel_in_place(std::int32_t dx, std::int32_t dy) {
    InputBatch batch;
    add_wheel(batch, dx, dy);
    if (batch.empty()) return;
    const std::size_t wanted = batch.records().size();
    if (InputPort::shared().send(batch) != wanted) {
        RWIN_WARN("input", "wheel {},{} was not injected (error {})", dx, dy, ::GetLastError());
        return;
    }
    RWIN_TRACE("input", "wheel {},{}", dx, dy);
}

void nudge_pointer(std::int32_t dx, std::int32_t dy) {
    InputBatch batch;
    batch.add_mouse(dx, dy, 0, MOUSEEVENTF_MOVE);
    if (InputPort::shared().send(batch) != 1) {
        RWIN_WARN("input", "relative move {},{} refused: error {}", dx, dy, ::GetLastError());
    }
}

}
