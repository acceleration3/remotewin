#pragma once

#include "proto/picture_rect.hpp"

#include <cstdint>
#include <vector>

namespace rwin::guest {
class ChangedTiles {
public:
    static constexpr std::uint32_t kTile = 64;

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

}
