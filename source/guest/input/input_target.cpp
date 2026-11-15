#include "guest/input/input_target.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

bool button_down() noexcept {
    for (const int key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}) {
        if ((::GetAsyncKeyState(key) & 0x8000) != 0) return true;
    }
    return false;
}

}

void InputTarget::raise(void* window, std::int32_t x, std::int32_t y, bool now) {
    const auto hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr || ::IsWindowEnabled(hwnd) == FALSE || button_down()) return;
    HWND under = ::WindowFromPoint(POINT{x, y});
    if (under == nullptr) return;
    under = ::GetAncestor(under, GA_ROOT);
    if (under == hwnd || under == ::GetAncestor(hwnd, GA_ROOT)) return;
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (!now) flags |= SWP_ASYNCWINDOWPOS;
    const bool stays_topmost = (::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    const bool raised = ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags) != FALSE
                        && (stays_topmost || ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags) != FALSE);
    if (!raised) {
        RWIN_DEBUG("input", "{:#x} could not be raised over {:#x}: error {}", reinterpret_cast<std::uintptr_t>(hwnd),
                   reinterpret_cast<std::uintptr_t>(under), ::GetLastError());
        return;
    }
    RWIN_TRACE("input", "{:#x} raised over {:#x} for input at {},{}", reinterpret_cast<std::uintptr_t>(hwnd),
               reinterpret_cast<std::uintptr_t>(under), x, y);
}

}
