#include "guest/windows/window_focus.hpp"

#include "logging/logger.hpp"
#include "proto/owned_window.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

bool WindowFocus::something_is_tracking() {
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (::GetGUIThreadInfo(0, &gui) == FALSE) return false;

    constexpr DWORD kMenuModes =
        GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE;
    return (gui.flags & kMenuModes) != 0 || gui.hwndMenuOwner != nullptr
           || gui.hwndCapture != nullptr;
}

void WindowFocus::give_to(std::uint64_t window_handle) const {
    auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    if (::IsWindow(window) == FALSE || ::GetForegroundWindow() == window) return;

    if (something_is_tracking()) {
        RWIN_TRACE("focus", "{:#x} not raised: something is tracking", window_handle);
        return;
    }
    // An owned window mirrored as a popup - a dropdown, a menu - is not
    // the one that should hold the foreground: its owner already does. One
    // mirrored as a window of its own - a dialog - is, once the user is in
    // it on the host.
    const auto style = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_STYLE));
    const auto extended = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_EXSTYLE));
    if (::GetWindow(window, GW_OWNER) != nullptr && !owned_window_stands_alone(style, extended)) {
        return;
    }
    // One of this window's own already has it - a dialog it owns, which
    // the press then landing on this window deactivates the way Windows
    // does, or keeps when the press lands on the dialog.
    if (HWND foreground = ::GetForegroundWindow();
        foreground != nullptr && ::GetAncestor(foreground, GA_ROOTOWNER) == window) {
        return;
    }
    // "does not become the foreground window when the user clicks it".
    if ((::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0) return;

    if (::IsIconic(window) != FALSE) ::ShowWindow(window, SW_RESTORE);
    ::AllowSetForegroundWindow(ASFW_ANY);
    ::SetForegroundWindow(window);
    RWIN_DEBUG("focus", "raised {:#x}", window_handle);
}

}  // namespace rwin::guest
