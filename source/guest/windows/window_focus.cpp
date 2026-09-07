#include "guest/windows/window_focus.hpp"

#include "logging/logger.hpp"

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
    // An owned window — a dialog, a palette, a dropdown — is not the one
    // that should hold the foreground. Its owner already does.
    if (::GetWindow(window, GW_OWNER) != nullptr) return;

    if (::IsIconic(window) != FALSE) ::ShowWindow(window, SW_RESTORE);
    ::AllowSetForegroundWindow(ASFW_ANY);
    ::SetForegroundWindow(window);
    RWIN_DEBUG("focus", "raised {:#x}", window_handle);
}

}  // namespace rwin::guest
