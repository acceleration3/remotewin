#pragma once

#include "proto/picture_rect.hpp"

#include <cstdint>
#include <vector>

namespace rwin::guest {

// Which parts of a picture changed since the last one, in whole tiles,
// so that only those cross the wire (FramePatch).
//
// A whole monitor is 14.7 MB a frame at 2560x1440, and QEMU's user-mode
// network carries a little over 100 MB a second: whole frames are eight a
// second at best, while what usually changes between two of them - a
// caret, a menu, a button lighting up - is a few tiles.
//
// Tiles changed side by side in a row of tiles are one rectangle.
class ChangedTiles {
public:
    // The side of a tile, in pixels.
    static constexpr std::uint32_t kTile = 64;

    // Weighs ``pixels`` (BGRA, ``width * 4`` bytes per row) against the
    // picture weighed last, and keeps them for the next. False when the
    // whole picture is the thing to send - the first, one of a new size,
    // or one more than half changed - and true when changed() is: possibly
    // nothing at all.
    [[nodiscard]] bool weigh(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height);

    [[nodiscard]] const std::vector<PictureRect>& changed() const noexcept { return changed_; }

private:
    [[nodiscard]] bool tile_differs(const std::uint8_t* pixels, const PictureRect& tile) const;
    void keep(const std::uint8_t* pixels, const PictureRect& rect);

    std::vector<std::uint8_t> previous_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<PictureRect> changed_;
};

}  // namespace rwin::guest
