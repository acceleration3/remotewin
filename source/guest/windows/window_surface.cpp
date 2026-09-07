#include "guest/windows/window_surface.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

WindowSurface::WindowSurface(std::uint64_t window_handle)
    : handle_(reinterpret_cast<void*>(static_cast<std::uintptr_t>(window_handle))) {
    auto window = static_cast<HWND>(handle_);
    if (::IsWindow(window) == FALSE) return;
    minimized_ = ::IsIconic(window) != FALSE;

    RECT rect{};
    if (::GetWindowRect(window, &rect) == FALSE) return;
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    left_ = rect.left;
    top_ = rect.top;
    width_ = static_cast<std::uint32_t>(rect.right - rect.left);
    height_ = static_cast<std::uint32_t>(rect.bottom - rect.top);
    valid_ = true;
}

}  // namespace rwin::guest
