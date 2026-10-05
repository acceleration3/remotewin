#include "guest/windows/window_resizer.hpp"

#include "guest/windows/window_surface.hpp"
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

    if (::IsZoomed(window) != FALSE) {
        // A maximised window ignores a size; it has to stop being
        // maximised before it can be given one.
        ::ShowWindow(window, SW_RESTORE);
    }
    // Read after any restore: a maximised window's border is not the one
    // it has restored.
    const WindowSurface surface{window_handle};
    if (!surface.valid()) return;

    const std::uint32_t room_width = room_.width();
    const std::uint32_t room_height = room_.height();
    const std::uint32_t wanted_width = std::min(width, room_width);
    const std::uint32_t wanted_height = std::min(height, room_height);
    const auto kept = [](std::int32_t at, std::int32_t room_from, std::uint32_t room_size,
                         std::uint32_t size) {
        return std::clamp(at, room_from, room_from + static_cast<std::int32_t>(room_size - size));
    };
    const int x = kept(surface.screen_x(0), room_.left(), room_width, wanted_width) - surface.inset_x();
    const int y = kept(surface.screen_y(0), room_.top(), room_height, wanted_height) - surface.inset_y();

    ::SetWindowPos(window, nullptr, x, y,
                   static_cast<int>(wanted_width + surface.border_width()),
                   static_cast<int>(wanted_height + surface.border_height()),
                   SWP_NOZORDER | SWP_NOACTIVATE);
    RWIN_DEBUG("resize", "{:#x} picture to {}x{}, window at {},{} (asked {}x{})",
               window_handle, wanted_width, wanted_height, x, y, width, height);
}

}  // namespace rwin::guest
