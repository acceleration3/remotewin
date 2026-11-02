#pragma once

#include "host/geometry/point_f.hpp"

namespace rwin::host {
struct BoxF {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] constexpr PointF corner() const noexcept { return PointF{x, y}; }
    [[nodiscard]] constexpr bool operator==(const BoxF&) const noexcept = default;
};

}
