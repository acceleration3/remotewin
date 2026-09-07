#include "guest/windows/window_surface.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

namespace rwin::guest {

WindowSurface::WindowSurface(std::uint64_t window_handle)
    : handle_(reinterpret_cast<void*>(static_cast<std::uintptr_t>(window_handle))) {
    auto window = static_cast<HWND>(handle_);
    if (::IsWindow(window) == FALSE) return;

    RECT window_rect{};
    if (::GetWindowRect(window, &window_rect) == FALSE) return;

    RECT frame = window_rect;
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame,
                                       sizeof(frame)))) {
        frame = window_rect;
    }
    if (frame.right <= frame.left || frame.bottom <= frame.top) return;

    frame_left_ = frame.left;
    frame_top_ = frame.top;
    width_ = static_cast<std::uint32_t>(frame.right - frame.left);
    height_ = static_cast<std::uint32_t>(frame.bottom - frame.top);
    captured_width_ = static_cast<std::uint32_t>(window_rect.right - window_rect.left);
    captured_height_ = static_cast<std::uint32_t>(window_rect.bottom - window_rect.top);
    // A maximised window's frame can start outside its own window rect;
    // clamped, because it indexes into the captured bitmap.
    inset_x_ = static_cast<std::uint32_t>(
        frame.left > window_rect.left ? frame.left - window_rect.left : 0);
    inset_y_ = static_cast<std::uint32_t>(
        frame.top > window_rect.top ? frame.top - window_rect.top : 0);
    if (inset_x_ + width_ > captured_width_) width_ = captured_width_ - inset_x_;
    if (inset_y_ + height_ > captured_height_) height_ = captured_height_ - inset_y_;
    valid_ = true;
}

}  // namespace rwin::guest
