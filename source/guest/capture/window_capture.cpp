#include "guest/capture/window_capture.hpp"

#include "guest/windows/window_surface.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstring>

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

// Undocumented in older SDK headers, and what makes PrintWindow copy a
// composited window instead of asking it to redraw itself into the DC —
// without it a DWM-composited window comes back blank.
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
    captured_ = nullptr;
    captured_width_ = 0;
    captured_height_ = 0;
    width_ = 0;
    height_ = 0;
}

bool WindowCapture::resize(const WindowSurface& surface) {
    release();

    HDC screen = ::GetDC(nullptr);
    HDC dc = ::CreateCompatibleDC(screen);
    ::ReleaseDC(nullptr, screen);
    if (dc == nullptr) return false;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(surface.captured_width());
    // Negative height is a top-down DIB, which is the row order the host
    // wants; a bottom-up one would have to be flipped on every frame.
    info.bmiHeader.biHeight = -static_cast<LONG>(surface.captured_height());
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
    captured_ = static_cast<std::uint8_t*>(bits);
    captured_width_ = surface.captured_width();
    captured_height_ = surface.captured_height();
    width_ = surface.width();
    height_ = surface.height();
    visible_.assign(static_cast<std::size_t>(width_) * height_ * kBytesPerPixel, 0);
    return true;
}

void WindowCapture::crop(const WindowSurface& surface) {
    const std::size_t captured_stride =
        static_cast<std::size_t>(captured_width_) * kBytesPerPixel;
    const std::size_t visible_stride = static_cast<std::size_t>(width_) * kBytesPerPixel;
    const std::size_t left = static_cast<std::size_t>(surface.inset_x()) * kBytesPerPixel;

    for (std::uint32_t row = 0; row < height_; ++row) {
        std::memcpy(visible_.data() + row * visible_stride,
                    captured_ + (surface.inset_y() + row) * captured_stride + left,
                    visible_stride);
    }
}

bool WindowCapture::grab(const WindowSurface& surface) {
    if (!surface.valid()) return false;

    if ((surface.captured_width() != captured_width_
         || surface.captured_height() != captured_height_
         || surface.width() != width_ || surface.height() != height_)
        && !resize(surface)) {
        return false;
    }
    if (::PrintWindow(static_cast<HWND>(surface.handle()),
                      static_cast<HDC>(device_context_), kRenderFullContent)
        == FALSE) {
        return false;
    }
    crop(surface);
    return true;
}

}  // namespace rwin::guest
