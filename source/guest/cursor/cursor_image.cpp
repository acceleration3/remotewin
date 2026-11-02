#include "guest/cursor/cursor_image.hpp"

#include "guest/cursor/monochrome_picture.hpp"

#include <utility>

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

BITMAPINFO top_down_32bpp(std::uint32_t width, std::uint32_t height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return info;
}

bool any_alpha(const std::vector<std::uint8_t>& pixels) {
    for (std::size_t at = 3; at < pixels.size(); at += kBytesPerPixel) {
        if (pixels[at] != 0) return true;
    }
    return false;
}

}

CursorImage::CursorImage(void* cursor_handle) {
    if (cursor_handle == nullptr) return;

    ICONINFO icon{};
    if (::GetIconInfo(static_cast<HCURSOR>(cursor_handle), &icon) == FALSE) return;
    hotspot_x_ = icon.xHotspot;
    hotspot_y_ = icon.yHotspot;

    HDC screen = ::GetDC(nullptr);
    if (icon.hbmColor != nullptr) {
        valid_ = read_colour(screen, icon.hbmColor);
        if (valid_ && !any_alpha(pixels_)) {
            apply_mask_alpha(screen, icon.hbmMask);
        }
    } else {
        valid_ = read_monochrome(screen, icon.hbmMask);
    }
    ::ReleaseDC(nullptr, screen);

    if (icon.hbmColor != nullptr) ::DeleteObject(icon.hbmColor);
    if (icon.hbmMask != nullptr) ::DeleteObject(icon.hbmMask);
}

bool CursorImage::read_colour(void* device_context, void* colour_bitmap) {
    BITMAP described{};
    if (::GetObjectW(static_cast<HBITMAP>(colour_bitmap), sizeof(described), &described)
        == 0) {
        return false;
    }
    width_ = static_cast<std::uint32_t>(described.bmWidth);
    height_ = static_cast<std::uint32_t>(described.bmHeight);
    pixels_.assign(static_cast<std::size_t>(width_) * height_ * kBytesPerPixel, 0);

    BITMAPINFO info = top_down_32bpp(width_, height_);
    return ::GetDIBits(static_cast<HDC>(device_context),
                       static_cast<HBITMAP>(colour_bitmap), 0, height_, pixels_.data(),
                       &info, DIB_RGB_COLORS)
           != 0;
}

void CursorImage::apply_mask_alpha(void* device_context, void* mask_bitmap) {
    if (mask_bitmap == nullptr) return;
    std::vector<std::uint8_t> mask(pixels_.size(), 0);
    BITMAPINFO info = top_down_32bpp(width_, height_);
    if (::GetDIBits(static_cast<HDC>(device_context), static_cast<HBITMAP>(mask_bitmap), 0,
                    height_, mask.data(), &info, DIB_RGB_COLORS)
        == 0) {
        return;
    }
    for (std::size_t at = 0; at < pixels_.size(); at += kBytesPerPixel) {
        pixels_[at + 3] = mask[at] != 0 ? 0 : 0xFF;
    }
}

bool CursorImage::read_monochrome(void* device_context, void* mask_bitmap) {
    BITMAP described{};
    if (::GetObjectW(static_cast<HBITMAP>(mask_bitmap), sizeof(described), &described)
        == 0) {
        return false;
    }
    width_ = static_cast<std::uint32_t>(described.bmWidth);
    height_ = static_cast<std::uint32_t>(described.bmHeight) / 2;
    if (width_ == 0 || height_ == 0) return false;

    const std::uint32_t both = height_ * 2;
    std::vector<std::uint8_t> planes(
        static_cast<std::size_t>(width_) * both * kBytesPerPixel, 0);
    BITMAPINFO info = top_down_32bpp(width_, both);
    if (::GetDIBits(static_cast<HDC>(device_context), static_cast<HBITMAP>(mask_bitmap), 0,
                    both, planes.data(), &info, DIB_RGB_COLORS)
        == 0) {
        return false;
    }
    MonochromePicture picture = monochrome_picture(planes, width_, height_);
    pixels_ = std::move(picture.bgra);
    inverts_ = picture.inverts;
    return !pixels_.empty();
}

std::vector<std::uint8_t> CursorImage::take_pixels() noexcept { return std::move(pixels_); }

}
