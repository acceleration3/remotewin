#include "guest/cursor/monochrome_picture.hpp"

#include <cstddef>

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;
constexpr std::uint8_t kOpaque = 0xFF;
constexpr std::uint8_t kWhite = 0xFF;

void paint(std::uint8_t* pixel, std::uint8_t shade) {
    pixel[0] = shade;
    pixel[1] = shade;
    pixel[2] = shade;
    pixel[3] = kOpaque;
}

// White on every transparent pixel beside an inverting one, so that what
// was drawn to invert is black ringed with white: seen over anything.
void edge(MonochromePicture& picture, const std::vector<bool>& inverted, std::size_t width, std::size_t height) {
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t at = y * width + x;
            std::uint8_t* pixel = &picture.bgra[at * kBytesPerPixel];
            if (pixel[3] != 0) continue;
            const bool beside = (x > 0 && inverted[at - 1]) || (x + 1 < width && inverted[at + 1])
                                || (y > 0 && inverted[at - width]) || (y + 1 < height && inverted[at + width]);
            if (beside) paint(pixel, kWhite);
        }
    }
}

}  // namespace

MonochromePicture monochrome_picture(std::span<const std::uint8_t> planes, std::uint32_t width,
                                     std::uint32_t height) {
    MonochromePicture picture;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const std::size_t half = pixels * kBytesPerPixel;
    if (pixels == 0 || planes.size() < 2 * half) return picture;
    picture.bgra.assign(half, 0);
    std::vector<bool> inverted(pixels, false);
    for (std::size_t at = 0; at < pixels; ++at) {
        const bool keep_background = planes[at * kBytesPerPixel] != 0;
        const bool flips = planes[half + at * kBytesPerPixel] != 0;
        if (keep_background && !flips) continue;
        inverted[at] = keep_background && flips;
        picture.inverts = picture.inverts || inverted[at];
        paint(&picture.bgra[at * kBytesPerPixel], !keep_background && flips ? kWhite : 0);
    }
    if (picture.inverts) edge(picture, inverted, width, height);
    return picture;
}

}  // namespace rwin::guest
