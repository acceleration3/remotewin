#include "guest/capture/changed_tiles.hpp"

#include <algorithm>
#include <cstring>

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

}

bool ChangedTiles::tile_differs(const std::uint8_t* pixels, const PictureRect& tile) const {
    const std::size_t stride = static_cast<std::size_t>(width_) * kBytesPerPixel;
    const std::size_t from = static_cast<std::size_t>(tile.x) * kBytesPerPixel;
    const std::size_t row = static_cast<std::size_t>(tile.width) * kBytesPerPixel;
    for (std::uint32_t y = tile.y; y < tile.y + tile.height; ++y) {
        const std::size_t at = y * stride + from;
        if (std::memcmp(pixels + at, previous_.data() + at, row) != 0) return true;
    }
    return false;
}

void ChangedTiles::keep(const std::uint8_t* pixels, const PictureRect& rect) {
    const std::size_t stride = static_cast<std::size_t>(width_) * kBytesPerPixel;
    const std::size_t from = static_cast<std::size_t>(rect.x) * kBytesPerPixel;
    const std::size_t row = static_cast<std::size_t>(rect.width) * kBytesPerPixel;
    for (std::uint32_t y = rect.y; y < rect.y + rect.height; ++y) {
        const std::size_t at = y * stride + from;
        std::memcpy(previous_.data() + at, pixels + at, row);
    }
}

bool ChangedTiles::weigh(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height) {
    changed_.clear();
    const std::size_t bytes = static_cast<std::size_t>(width) * height * kBytesPerPixel;
    if (width != width_ || height != height_ || previous_.size() != bytes) {
        width_ = width;
        height_ = height;
        previous_.assign(pixels, pixels + bytes);
        return false;
    }

    std::uint64_t changed_area = 0;
    for (std::uint32_t top = 0; top < height_; top += kTile) {
        const std::uint32_t rows = std::min(kTile, height_ - top);
        std::uint32_t run_left = 0;
        bool in_run = false;
        const std::uint32_t columns = (width_ + kTile - 1) / kTile;
        for (std::uint32_t column = 0; column <= columns; ++column) {
            const std::uint32_t left = column * kTile;
            const bool differs =
                column < columns
                && tile_differs(pixels, PictureRect{left, top, std::min(kTile, width_ - left), rows});
            if (differs && !in_run) {
                run_left = left;
                in_run = true;
            }
            if (!differs && in_run) {
                const PictureRect run{run_left, top, std::min(left, width_) - run_left, rows};
                changed_.push_back(run);
                changed_area += static_cast<std::uint64_t>(run.width) * run.height;
                in_run = false;
            }
        }
    }

    if (changed_area * 2 > static_cast<std::uint64_t>(width_) * height_) {
        changed_.clear();
        previous_.assign(pixels, pixels + bytes);
        return false;
    }
    for (const PictureRect& rect : changed_) keep(pixels, rect);
    return true;
}

}
