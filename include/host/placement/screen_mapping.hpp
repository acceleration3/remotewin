#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"

#include <cmath>

namespace rwin::host {

// Where a point of the guest's screen is on this desktop, and back.
//
// The guest's screen is this desktop's output pixel for pixel: the guest
// is given the output's size and its DPI is set to the output's scale, so
// a guest pixel is a device pixel of the output. A device pixel is 1/scale
// of a logical unit, and logical units are what the compositor places
// windows in, counted from the output's corner in the desktop's layout. At
// 120% the guest's 152 is 126.67 logical units in; the compositor keeps
// that fraction, and 126.67 is 152 again on the way back.
class ScreenMapping {
public:
    // ``origin`` is the output's corner in the desktop's logical layout;
    // ``scale`` its device pixels per logical unit.
    ScreenMapping(PointF origin, double scale) noexcept : origin_(origin), scale_(scale) {}

    [[nodiscard]] PointF host_point(Point guest) const noexcept {
        return PointF{origin_.x + guest.x / scale_, origin_.y + guest.y / scale_};
    }

    // Device pixels per logical unit.
    [[nodiscard]] double scale() const noexcept { return scale_; }

    // The nearest guest pixel.
    [[nodiscard]] Point guest_point(PointF host) const noexcept {
        return Point{static_cast<int>(std::lround((host.x - origin_.x) * scale_)),
                     static_cast<int>(std::lround((host.y - origin_.y) * scale_))};
    }

private:
    PointF origin_;
    double scale_;
};

}  // namespace rwin::host
