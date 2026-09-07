#include "guest/windows/window_state.hpp"

#include "guest/windows/desktop_stand_in.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

void WindowState::apply(std::uint64_t window_handle, bool minimized, bool maximized,
                        bool active) const {
    auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    if (::IsWindow(window) == FALSE) return;
    if (!active && ::GetForegroundWindow() == window) {
        elsewhere_.take_foreground();
        RWIN_DEBUG("state", "{:#x} deactivated on the host; foreground given to the desktop",
                   window_handle);
    }
    const bool is_minimized = ::IsIconic(window) != FALSE;
    const bool is_maximized = ::IsZoomed(window) != FALSE;
    if (minimized == is_minimized && maximized == is_maximized) return;

    // SW_MINIMIZE rather than SW_SHOWMINIMIZED: the mirror lost the
    // foreground to whatever the user minimised it for, and Windows
    // activates the next window the same way.
    int show = SW_RESTORE;
    if (minimized) {
        show = SW_MINIMIZE;
    } else if (maximized) {
        show = SW_MAXIMIZE;
    }
    ::ShowWindow(window, show);
    RWIN_DEBUG("state", "{:#x} minimized={} maximized={}", window_handle, minimized,
               maximized);
}

}  // namespace rwin::guest
