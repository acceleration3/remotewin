#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"

#include <cstdint>

namespace rwin::host {

// An xdg_positioner's worth: the popup's size, the rectangle of its parent's
// window geometry it opens from, the edge of that rectangle and the
// direction it opens in (xdg_positioner's anchor and gravity values), how it
// may give way at an output's edge (constraint_adjustment bits), and an
// offset from where that puts it. Logical units of the parent.
struct PopupPlacement {
    Size size;
    Box anchor;
    std::uint32_t edge = 0;
    std::uint32_t gravity = 0;
    std::uint32_t adjust = 0;
    Point offset;

    [[nodiscard]] bool operator==(const PopupPlacement&) const noexcept = default;
};

}  // namespace rwin::host
