#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"

namespace rwin::host {
class ViewMapping {
public:
    ViewMapping() = default;

    [[nodiscard]] static ViewMapping one_to_one(Size picture, double scale) noexcept;
    [[nodiscard]] static ViewMapping stretched(Size picture, Size logical) noexcept;

    [[nodiscard]] static Size picture_for(Size logical, double scale) noexcept;

    [[nodiscard]] ViewMapping within(Size most) const noexcept;

    [[nodiscard]] Size logical() const noexcept { return logical_; }
    [[nodiscard]] double source_width() const noexcept { return logical_.width * x_; }
    [[nodiscard]] double source_height() const noexcept { return logical_.height * y_; }

    [[nodiscard]] Point picture_point(PointF logical) const noexcept;
    [[nodiscard]] PointF logical_point(Point picture) const noexcept;
    [[nodiscard]] Box logical_box(const Box& picture) const noexcept;
    [[nodiscard]] Box logical_box_inside(const Box& picture) const noexcept;

private:
    ViewMapping(Size logical, double x, double y) noexcept : logical_(logical), x_(x), y_(y) {}

    Size logical_;
    double x_ = 1.0;
    double y_ = 1.0;
};

}
