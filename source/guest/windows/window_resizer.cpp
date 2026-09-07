#include "guest/windows/window_resizer.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>

namespace rwin::guest {

void WindowResizer::resize(std::uint64_t window_handle, std::uint32_t width,
                           std::uint32_t height) const {
    auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    if (::IsWindow(window) == FALSE || width == 0 || height == 0) return;

    // The work area, not the whole screen: growing under the taskbar
    // gives back pixels the capture cannot show anyway.
    RECT work{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) == FALSE) return;
    const auto available_width = static_cast<std::uint32_t>(work.right - work.left);
    const auto available_height = static_cast<std::uint32_t>(work.bottom - work.top);

    const std::uint32_t wanted_width = std::min(width, available_width);
    const std::uint32_t wanted_height = std::min(height, available_height);
    const int x = work.left + static_cast<int>((available_width - wanted_width) / 2);
    const int y = work.top + static_cast<int>((available_height - wanted_height) / 2);

    if (::IsZoomed(window) != FALSE) {
        // A maximised window ignores a size; it has to stop being
        // maximised before it can be given one.
        ::ShowWindow(window, SW_RESTORE);
    }
    ::SetWindowPos(window, nullptr, x, y, static_cast<int>(wanted_width),
                   static_cast<int>(wanted_height), SWP_NOZORDER | SWP_NOACTIVATE);
    RWIN_DEBUG("resize", "{:#x} to {}x{} centred at {},{} (asked {}x{})", window_handle,
               wanted_width, wanted_height, x, y, width, height);
}

}  // namespace rwin::guest
