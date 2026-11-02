#pragma once

namespace rwin::host {
struct PointF {
    double x = 0.0;
    double y = 0.0;

    [[nodiscard]] constexpr bool operator==(const PointF&) const noexcept = default;
};

}
