#include "probe/test_pattern.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::probe {

namespace {

constexpr std::uint32_t kTopLeft = 0x00D02828;
constexpr std::uint32_t kTopRight = 0x0028B048;
constexpr std::uint32_t kBottomLeft = 0x002848D0;
constexpr std::uint32_t kBottomRight = 0x00E0C828;
constexpr std::uint32_t kCheckDark = 0x00000000;
constexpr std::uint32_t kCheckLight = 0x00FFFFFF;
constexpr std::int32_t kCheckRows = 8;

// A COLORREF is 0x00BBGGRR; a DIB pixel is 0x00RRGGBB.
std::uint32_t dib_from_colorref(std::uint32_t colorref) {
    return ((colorref & 0xFF) << 16) | (colorref & 0xFF00) | ((colorref >> 16) & 0xFF);
}

}  // namespace

void TestPattern::fill(std::int32_t width, std::int32_t height,
                       std::optional<std::uint32_t> key) {
    pixels_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const std::int32_t middle_x = width / 2;
    const std::int32_t middle_y = height / 2;
    for (std::int32_t y = 0; y < height; ++y) {
        std::uint32_t* row = pixels_.data() + static_cast<std::size_t>(y) * width;
        for (std::int32_t x = 0; x < width; ++x) {
            if (y < kCheckRows) {
                row[x] = ((x + y) % 2) == 0 ? kCheckDark : kCheckLight;
                continue;
            }
            const bool left = x < middle_x;
            const bool top = y < middle_y;
            row[x] = top ? (left ? kTopLeft : kTopRight) : (left ? kBottomLeft : kBottomRight);
        }
    }
    if (!key.has_value()) return;
    const std::uint32_t keyed = dib_from_colorref(*key);
    for (std::int32_t y = height / 3; y < height * 2 / 3; ++y) {
        std::uint32_t* row = pixels_.data() + static_cast<std::size_t>(y) * width;
        for (std::int32_t x = width / 3; x < width * 2 / 3; ++x) row[x] = keyed;
    }
}

void TestPattern::paint(void* device_context, std::int32_t width, std::int32_t height,
                        std::optional<std::uint32_t> key) {
    if (width <= 0 || height <= 0) return;
    fill(width, height, key);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    // Negative: top-down, the order the rows were filled in.
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    ::SetDIBitsToDevice(static_cast<HDC>(device_context), 0, 0, static_cast<DWORD>(width),
                        static_cast<DWORD>(height), 0, 0, 0, static_cast<UINT>(height),
                        pixels_.data(), &info, DIB_RGB_COLORS);
}

}  // namespace rwin::probe
