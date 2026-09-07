#include "guest/capture/content_bounds.hpp"

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;
constexpr std::size_t kAlpha = 3;

bool row_has_content(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t y) {
    const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * width * kBytesPerPixel;
    for (std::uint32_t x = 0; x < width; ++x) {
        if (row[static_cast<std::size_t>(x) * kBytesPerPixel + kAlpha] != 0) return true;
    }
    return false;
}

bool column_has_content(const std::uint8_t* pixels, std::uint32_t width,
                        std::uint32_t top, std::uint32_t bottom, std::uint32_t x) {
    for (std::uint32_t y = top; y <= bottom; ++y) {
        const std::size_t at =
            (static_cast<std::size_t>(y) * width + x) * kBytesPerPixel + kAlpha;
        if (pixels[at] != 0) return true;
    }
    return false;
}

}  // namespace

ContentBounds::ContentBounds(const std::uint8_t* pixels, std::uint32_t width,
                             std::uint32_t height) {
    if (pixels == nullptr || width == 0 || height == 0) return;

    std::uint32_t top = 0;
    while (top < height && !row_has_content(pixels, width, top)) ++top;
    if (top == height) return;  // nothing drawn at all

    std::uint32_t bottom = height - 1;
    while (bottom > top && !row_has_content(pixels, width, bottom)) --bottom;

    std::uint32_t left = 0;
    while (left < width && !column_has_content(pixels, width, top, bottom, left)) ++left;

    std::uint32_t right = width - 1;
    while (right > left && !column_has_content(pixels, width, top, bottom, right)) --right;

    left_ = left;
    top_ = top;
    width_ = right - left + 1;
    height_ = bottom - top + 1;
}

}  // namespace rwin::guest
