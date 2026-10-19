#pragma once

#include "host/geometry/point_f.hpp"

namespace rwin::host {

// A rectangle between whole units: a window's frame in the compositor's
// logical layout, which keeps the fraction a fractional scale leaves.
struct BoxF {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] constexpr PointF corner() const noexcept { return PointF{x, y}; }
    [[nodiscard]] constexpr bool operator==(const BoxF&) const noexcept = default;
};

}  // namespace rwin::host
