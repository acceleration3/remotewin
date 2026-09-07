#include "guest/windows/window_resizer.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

void WindowResizer::resize(std::uint64_t window_handle, std::uint32_t width,
                           std::uint32_t height) const {
    auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    if (::IsWindow(window) == FALSE || width == 0 || height == 0) return;

    if (::IsZoomed(window) != FALSE) {
        // A maximised window ignores a size; it has to stop being
        // maximised before it can be given one.
        ::ShowWindow(window, SW_RESTORE);
    }
    ::SetWindowPos(window, nullptr, 0, 0, static_cast<int>(width),
                   static_cast<int>(height),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RWIN_DEBUG("resize", "{:#x} to {}x{}", window_handle, width, height);
}

}  // namespace rwin::guest
