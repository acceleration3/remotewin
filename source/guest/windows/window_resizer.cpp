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

    const std::uint32_t room_width = room_.width();
    const std::uint32_t room_height = room_.height();
    const std::uint32_t wanted_width = std::min(width, room_width);
    const std::uint32_t wanted_height = std::min(height, room_height);
    const int x = room_.left() + static_cast<int>((room_width - wanted_width) / 2);
    const int y = room_.top() + static_cast<int>((room_height - wanted_height) / 2);

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
