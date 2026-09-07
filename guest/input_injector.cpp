#include "guest/input_injector.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

HWND from(std::uint64_t handle) {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle));
}

// The deepest child under the point, in that child's own client
// coordinates: a control only understands a position measured from its
// own corner.
HWND target_at(HWND top, std::int32_t x, std::int32_t y, POINT& client) {
    POINT in_top{x, y};
    HWND child = ::RealChildWindowFromPoint(top, in_top);
    HWND target = child != nullptr ? child : top;
    client = in_top;
    if (target != top) {
        ::MapWindowPoints(top, target, &client, 1);
    }
    return target;
}

LPARAM packed(POINT p) {
    return MAKELPARAM(static_cast<WORD>(p.x), static_cast<WORD>(p.y));
}

}  // namespace

void InputInjector::move(std::uint64_t window_handle, std::int32_t x,
                         std::int32_t y) const {
    HWND top = from(window_handle);
    if (::IsWindow(top) == FALSE) return;
    POINT client{};
    HWND target = target_at(top, x, y, client);
    ::PostMessageW(target, WM_MOUSEMOVE, 0, packed(client));
}

void InputInjector::button(std::uint64_t window_handle, std::int32_t x, std::int32_t y,
                           Button button, bool pressed) const {
    HWND top = from(window_handle);
    if (::IsWindow(top) == FALSE) return;
    POINT client{};
    HWND target = target_at(top, x, y, client);

    UINT message = 0;
    WPARAM held = 0;
    switch (button) {
        case Button::Left:
            message = pressed ? WM_LBUTTONDOWN : WM_LBUTTONUP;
            held = pressed ? MK_LBUTTON : 0;
            break;
        case Button::Middle:
            message = pressed ? WM_MBUTTONDOWN : WM_MBUTTONUP;
            held = pressed ? MK_MBUTTON : 0;
            break;
        case Button::Right:
            message = pressed ? WM_RBUTTONDOWN : WM_RBUTTONUP;
            held = pressed ? MK_RBUTTON : 0;
            break;
    }
    // A control that never sees the click cannot take focus, and an Edit
    // without focus ignores every character that follows.
    if (pressed) {
        ::SetFocus(target);
    }
    ::PostMessageW(target, message, held, packed(client));
}

void InputInjector::key(std::uint64_t window_handle, std::uint32_t virtual_key,
                        std::uint32_t character, bool pressed) const {
    HWND top = from(window_handle);
    if (::IsWindow(top) == FALSE) return;
    HWND focused = ::GetFocus();
    HWND target = focused != nullptr ? focused : top;

    ::PostMessageW(target, pressed ? WM_KEYDOWN : WM_KEYUP,
                   static_cast<WPARAM>(virtual_key), 1);
    // WM_CHAR is what an Edit control actually types from; WM_KEYDOWN
    // alone moves the caret and inserts nothing.
    if (pressed && character != 0) {
        ::PostMessageW(target, WM_CHAR, static_cast<WPARAM>(character), 1);
    }
}

}  // namespace rwin::guest
