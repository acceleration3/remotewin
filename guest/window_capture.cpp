#include "guest/window_capture.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {
// Undocumented in the SDK headers old enough to lack it, but this is what
// makes PrintWindow capture a composited window rather than asking it to
// redraw itself into the DC — without it a DWM-composited window comes
// back blank.
constexpr UINT kRenderFullContent = 0x00000002;
}  // namespace

WindowCapture::WindowCapture() = default;

WindowCapture::~WindowCapture() { release(); }

void WindowCapture::release() {
    auto* dc = static_cast<HDC>(device_context_);
    if (dc != nullptr) {
        if (previous_bitmap_ != nullptr) {
            ::SelectObject(dc, static_cast<HGDIOBJ>(previous_bitmap_));
        }
        ::DeleteDC(dc);
    }
    if (bitmap_ != nullptr) {
        ::DeleteObject(static_cast<HGDIOBJ>(bitmap_));
    }
    device_context_ = nullptr;
    bitmap_ = nullptr;
    previous_bitmap_ = nullptr;
    pixels_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool WindowCapture::resize(std::uint32_t width, std::uint32_t height) {
    release();

    HDC screen = ::GetDC(nullptr);
    HDC dc = ::CreateCompatibleDC(screen);
    ::ReleaseDC(nullptr, screen);
    if (dc == nullptr) return false;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    // Negative height is a top-down DIB, which is the row order the host
    // wants; a bottom-up one would have to be flipped on every frame.
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        ::DeleteDC(dc);
        return false;
    }

    device_context_ = dc;
    bitmap_ = bitmap;
    previous_bitmap_ = ::SelectObject(dc, bitmap);
    pixels_ = static_cast<std::uint8_t*>(bits);
    width_ = width;
    height_ = height;
    return true;
}

bool WindowCapture::grab(std::uint64_t window_handle) {
    auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    if (::IsWindow(window) == FALSE) return false;

    RECT rect{};
    if (::GetWindowRect(window, &rect) == FALSE) return false;
    const auto width = static_cast<std::uint32_t>(rect.right - rect.left);
    const auto height = static_cast<std::uint32_t>(rect.bottom - rect.top);
    if (width == 0 || height == 0) return false;

    if ((width != width_ || height != height_) && !resize(width, height)) {
        return false;
    }
    return ::PrintWindow(window, static_cast<HDC>(device_context_), kRenderFullContent)
           != FALSE;
}

}  // namespace rwin::guest
