#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"

#include <cmath>

namespace rwin::host {
class ScreenMapping {
public:
    ScreenMapping(PointF origin, double scale) noexcept : origin_(origin), scale_(scale) {}

    [[nodiscard]] PointF host_point(Point guest) const noexcept {
        return PointF{origin_.x + guest.x / scale_, origin_.y + guest.y / scale_};
    }

    [[nodiscard]] double scale() const noexcept { return scale_; }

    [[nodiscard]] Point guest_point(PointF host) const noexcept {
        return Point{static_cast<int>(std::lround((host.x - origin_.x) * scale_)),
                     static_cast<int>(std::lround((host.y - origin_.y) * scale_))};
    }

private:
    PointF origin_;
    double scale_;
};

}
