#include "probe/capture_screen.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

#include <cstring>
#include <memory>

namespace rwin::probe {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

struct ScreenDcRelease {
    void operator()(HDC__* dc) const noexcept { ::ReleaseDC(nullptr, dc); }
};
struct DcDeleter {
    void operator()(HDC__* dc) const noexcept { ::DeleteDC(dc); }
};
struct BitmapDeleter {
    void operator()(HBITMAP__* bitmap) const noexcept { ::DeleteObject(bitmap); }
};

}

std::optional<ScreenPixels> capture_screen(std::int32_t left, std::int32_t top,
                                           std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) return std::nullopt;
    ::DwmFlush();

    const std::unique_ptr<HDC__, ScreenDcRelease> screen{::GetDC(nullptr)};
    const std::unique_ptr<HDC__, DcDeleter> memory{::CreateCompatibleDC(screen.get())};
    if (screen == nullptr || memory == nullptr) {
        RWIN_ERROR("capture", "no device context for the screen");
        return std::nullopt;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const std::unique_ptr<HBITMAP__, BitmapDeleter> bitmap{
        ::CreateDIBSection(screen.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
    if (bitmap == nullptr || bits == nullptr) {
        RWIN_ERROR("capture", "no {}x{} bitmap to read the screen into", width, height);
        return std::nullopt;
    }
    HGDIOBJ previous = ::SelectObject(memory.get(), bitmap.get());
    const BOOL copied = ::BitBlt(memory.get(), 0, 0, static_cast<int>(width),
                                 static_cast<int>(height), screen.get(), left, top,
                                 SRCCOPY | CAPTUREBLT);
    ::SelectObject(memory.get(), previous);
    if (copied == FALSE) {
        RWIN_ERROR("capture", "BitBlt of the screen at {},{} {}x{} failed: error {}", left, top,
                   width, height, ::GetLastError());
        return std::nullopt;
    }
    ::GdiFlush();

    ScreenPixels pixels;
    pixels.left = left;
    pixels.top = top;
    pixels.width = width;
    pixels.height = height;
    const std::size_t size = static_cast<std::size_t>(width) * height * kBytesPerPixel;
    pixels.bgra.resize(size);
    std::memcpy(pixels.bgra.data(), bits, size);
    for (std::size_t at = 3; at < size; at += kBytesPerPixel) pixels.bgra[at] = 0xFF;
    return pixels;
}

}
