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

bool topmost(HWND window) noexcept { return (::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; }

UINT reorder_flags(bool now) noexcept {
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (!now) flags |= SWP_ASYNCWINDOWPOS;
    return flags;
}

}

InputTarget::~InputTarget() { give_back_topmost(nullptr, true); }

void InputTarget::give_back_topmost(void* keep, bool now) noexcept {
    const auto lent = static_cast<HWND>(lent_topmost_);
    const auto kept = static_cast<HWND>(keep);
    if (lent == nullptr || lent == kept || (kept != nullptr && lent == ::GetAncestor(kept, GA_ROOTOWNER))) return;
    lent_topmost_ = nullptr;
    if (::IsWindow(lent) == FALSE) return;
    if (::SetWindowPos(lent, HWND_NOTOPMOST, 0, 0, 0, 0, reorder_flags(now)) == FALSE) {
        RWIN_DEBUG("input", "{:#x} could not leave the topmost band it was lent: error {}",
                   reinterpret_cast<std::uintptr_t>(lent), ::GetLastError());
    }
}

void InputTarget::raise(void* window, std::int32_t x, std::int32_t y, bool now) {
    const auto hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr || ::IsWindowEnabled(hwnd) == FALSE || button_down()) return;
    give_back_topmost(hwnd, now);
    HWND under = ::WindowFromPoint(POINT{x, y});
    if (under == nullptr) return;
    under = ::GetAncestor(under, GA_ROOT);
    if (under == hwnd || under == ::GetAncestor(hwnd, GA_ROOT)) return;
    const UINT flags = reorder_flags(now);
    const bool stays_topmost = topmost(hwnd);
    const bool lend_topmost = !stays_topmost && topmost(under);
    const bool raised = ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags) != FALSE
                        && (stays_topmost || lend_topmost || ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags) != FALSE);
    if (!raised) {
        RWIN_DEBUG("input", "{:#x} could not be raised over {:#x}: error {}", reinterpret_cast<std::uintptr_t>(hwnd),
                   reinterpret_cast<std::uintptr_t>(under), ::GetLastError());
        return;
    }
    if (lend_topmost) lent_topmost_ = hwnd;
    RWIN_TRACE("input", "{:#x} raised over {:#x} for input at {},{}", reinterpret_cast<std::uintptr_t>(hwnd),
               reinterpret_cast<std::uintptr_t>(under), x, y);
}

}
