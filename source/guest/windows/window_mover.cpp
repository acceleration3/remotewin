#include "guest/windows/window_mover.hpp"

#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

void WindowMover::move(const WindowSurface& surface, std::int32_t x, std::int32_t y) const {
    if (!surface.valid() || surface.minimized()) return;
    auto* window = static_cast<HWND>(surface.handle());
    // A maximised window is where its state puts it; moving it would be
    // restoring it, which the host did not ask for.
    if (::IsZoomed(window) != FALSE) return;
    const int left = x - surface.inset_x();
    const int top = y - surface.inset_y();
    if (::SetWindowPos(window, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)
        == FALSE) {
        RWIN_WARN("move", "{:#x} could not be moved to {},{}: error {}",
                  reinterpret_cast<std::uintptr_t>(window), x, y, ::GetLastError());
        return;
    }
    RWIN_DEBUG("move", "{:#x} picture to {},{} (window at {},{})", reinterpret_cast<std::uintptr_t>(window), x,
               y, left, top);
}

}  // namespace rwin::guest
