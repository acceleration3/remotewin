#include "guest/input/input_target.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

void InputTarget::raise(void* window, std::int32_t x, std::int32_t y, bool now) {
    const auto hwnd = static_cast<HWND>(window);
    if (held_ > 0 || hwnd == nullptr || ::IsWindowEnabled(hwnd) == FALSE) return;
    HWND under = ::WindowFromPoint(POINT{x, y});
    if (under == nullptr) return;
    under = ::GetAncestor(under, GA_ROOT);
    if (under == hwnd || under == ::GetAncestor(hwnd, GA_ROOT)) return;
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (!now) flags |= SWP_ASYNCWINDOWPOS;
    if (::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, flags) == FALSE) {
        RWIN_DEBUG("input", "{:#x} could not be raised over {:#x}: error {}", reinterpret_cast<std::uintptr_t>(hwnd),
                   reinterpret_cast<std::uintptr_t>(under), ::GetLastError());
        return;
    }
    RWIN_TRACE("input", "{:#x} raised over {:#x} for input at {},{}", reinterpret_cast<std::uintptr_t>(hwnd),
               reinterpret_cast<std::uintptr_t>(under), x, y);
}

void InputTarget::pressed(bool down) noexcept {
    held_ = down ? held_ + 1 : (held_ > 0 ? held_ - 1 : 0);
}

}  // namespace rwin::guest
