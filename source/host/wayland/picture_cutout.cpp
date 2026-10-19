#include "host/wayland/picture_cutout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace rwin::host {

namespace {

// Transparent, premultiplied: every byte nothing.
void clear(std::uint8_t* texels, int count) { std::memset(texels, 0, static_cast<std::size_t>(count) * 4); }

void cut_key(std::span<std::uint8_t> pixels, int stride, Size size, std::uint32_t key) {
    const auto red = static_cast<std::uint8_t>(key & 0xFF);
    const auto green = static_cast<std::uint8_t>((key >> 8) & 0xFF);
    const auto blue = static_cast<std::uint8_t>((key >> 16) & 0xFF);
    for (int row = 0; row < size.height; ++row) {
        std::uint8_t* texel = pixels.data() + static_cast<std::size_t>(row) * stride;
        for (int column = 0; column < size.width; ++column, texel += 4) {
            if (texel[0] == blue && texel[1] == green && texel[2] == red) clear(texel, 1);
        }
    }
}

void cut_outside(std::span<std::uint8_t> pixels, int stride, Size size, const std::vector<Box>& region) {
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(size.width));
    for (int row = 0; row < size.height; ++row) {
        std::fill(seen.begin(), seen.end(), std::uint8_t{0});
        for (const Box& box : region) {
            if (row < box.y || row >= box.bottom()) continue;
            const int from = std::clamp(box.x, 0, size.width);
            const int to = std::clamp(box.right(), 0, size.width);
            if (from < to) std::fill(seen.begin() + from, seen.begin() + to, std::uint8_t{1});
        }
        std::uint8_t* line = pixels.data() + static_cast<std::size_t>(row) * stride;
        for (int column = 0; column < size.width;) {
            if (seen[static_cast<std::size_t>(column)] != 0) {
                ++column;
                continue;
            }
            int end = column;
            while (end < size.width && seen[static_cast<std::size_t>(end)] == 0) ++end;
            clear(line + static_cast<std::size_t>(column) * 4, end - column);
            column = end;
        }
    }
}

}  // namespace

void PictureCutout::apply(std::span<std::uint8_t> pixels, int stride, Size size) const {
    if (color_key.has_value()) cut_key(pixels, stride, size, *color_key);
    if (!region.empty()) cut_outside(pixels, stride, size, region);
}

}  // namespace rwin::host
