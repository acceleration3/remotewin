#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/size.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rwin::host {

// What of a guest window's picture Windows does not show: the texels of a
// layered window's colour key (LWA_COLORKEY), and whatever lies outside its
// window region (SetWindowRgn). Cut out of a picture in pixels by making
// those texels transparent.
struct PictureCutout {
    // A COLORREF: 0x00BBGGRR.
    std::optional<std::uint32_t> color_key;
    // Picture rectangles; empty for no region, the whole picture.
    std::vector<Box> region;

    [[nodiscard]] bool none() const noexcept { return !color_key.has_value() && region.empty(); }

    // ``pixels`` are premultiplied BGRA rows ``stride`` bytes apart.
    void apply(std::span<std::uint8_t> pixels, int stride, Size size) const;

    [[nodiscard]] bool operator==(const PictureCutout&) const = default;
};

}  // namespace rwin::host
