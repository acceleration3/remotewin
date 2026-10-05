#include "guest/windows/window_surface.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

namespace rwin::guest {

namespace {

// The extended frame bounds, kept inside the window rectangle: DWM answers
// for a window it draws no frame for with the rectangle itself, and fails
// for one it does not compose at all, which is then its rectangle too.
RECT picture_of(HWND window, const RECT& rect) {
    RECT bounds{};
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds,
                                       sizeof(bounds)))
        || bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return rect;
    }
    RECT inside{};
    if (::IntersectRect(&inside, &bounds, &rect) == FALSE) return rect;
    return inside;
}

bool shaped(HWND window) {
    RECT region{};
    const int kind = ::GetWindowRgnBox(window, &region);
    return kind == SIMPLEREGION || kind == COMPLEXREGION;
}

bool dwm_framed(HWND window) {
    constexpr LONG_PTR kFrame = WS_BORDER | WS_DLGFRAME | WS_THICKFRAME;
    BOOL enabled = FALSE;
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_NCRENDERING_ENABLED, &enabled,
                                       sizeof(enabled)))) {
        return false;
    }
    return enabled != FALSE && (::GetWindowLongPtrW(window, GWL_STYLE) & kFrame) != 0;
}

}  // namespace

WindowSurface::WindowSurface(std::uint64_t window_handle)
    : handle_(reinterpret_cast<void*>(static_cast<std::uintptr_t>(window_handle))) {
    auto window = static_cast<HWND>(handle_);
    if (::IsWindow(window) == FALSE) return;
    minimized_ = ::IsIconic(window) != FALSE;

    RECT rect{};
    if (::GetWindowRect(window, &rect) == FALSE) return;
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const RECT picture = picture_of(window, rect);

    left_ = picture.left;
    top_ = picture.top;
    width_ = static_cast<std::uint32_t>(picture.right - picture.left);
    height_ = static_cast<std::uint32_t>(picture.bottom - picture.top);
    inset_x_ = picture.left - rect.left;
    inset_y_ = picture.top - rect.top;
    border_width_ = static_cast<std::uint32_t>(rect.right - rect.left) - width_;
    border_height_ = static_cast<std::uint32_t>(rect.bottom - rect.top) - height_;

    margin_is_reserved_ = !shaped(window) && !dwm_framed(window);
    valid_ = true;
}

}  // namespace rwin::guest
