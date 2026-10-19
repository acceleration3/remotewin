#pragma once

namespace rwin::host {

// A point between whole units: where Wayland says the pointer is, in a
// surface's logical units (wl_fixed_t).
struct PointF {
    double x = 0.0;
    double y = 0.0;

    [[nodiscard]] constexpr bool operator==(const PointF&) const noexcept = default;
};

}  // namespace rwin::host
