#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"

#include <cstdint>

namespace rwin::host {
struct PopupPlacement {
    Size size;
    Box anchor;
    std::uint32_t edge = 0;
    std::uint32_t gravity = 0;
    std::uint32_t adjust = 0;
    Point offset;

    [[nodiscard]] bool operator==(const PopupPlacement&) const noexcept = default;
};

}
