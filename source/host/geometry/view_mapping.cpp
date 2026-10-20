#include "host/geometry/view_mapping.hpp"

#include <algorithm>
#include <cmath>

namespace rwin::host {

namespace {

// Within this of a whole number is that number. A fractional scale comes in
// 120ths, so a size times one that is not whole misses a whole number by at
// least 1/120; 5 * 1.2 is 6, and a floor that saw 5.9999999 would be a
// pixel short.
constexpr double kWhole = 1e-6;

int floor_of(double value) { return static_cast<int>(std::floor(value + kWhole)); }

int ceil_of(double value) { return static_cast<int>(std::ceil(value - kWhole)); }

}  // namespace

// A side of fewer pixels than the scale covers no whole unit: it shows on
// one unit, over its own pixels and no more, or the viewport's source would
// reach past the buffer (wp_viewport.out_of_buffer, which ends the client).
ViewMapping ViewMapping::one_to_one(Size picture, double scale) noexcept {
    const int width = floor_of(picture.width / scale);
    const int height = floor_of(picture.height / scale);
    return ViewMapping{Size{std::max(1, width), std::max(1, height)},
                       width >= 1 ? scale : static_cast<double>(std::max(1, picture.width)),
                       height >= 1 ? scale : static_cast<double>(std::max(1, picture.height))};
}

ViewMapping ViewMapping::stretched(Size picture, Size logical) noexcept {
    const Size shown{std::max(1, logical.width), std::max(1, logical.height)};
    return ViewMapping{shown, static_cast<double>(std::max(1, picture.width)) / shown.width,
                       static_cast<double>(std::max(1, picture.height)) / shown.height};
}

Size ViewMapping::picture_for(Size logical, double scale) noexcept {
    return Size{ceil_of(logical.width * scale), ceil_of(logical.height * scale)};
}

ViewMapping ViewMapping::within(Size most) const noexcept {
    const Size cropped{std::clamp(most.width, 1, logical_.width), std::clamp(most.height, 1, logical_.height)};
    return ViewMapping{cropped, x_, y_};
}

Point ViewMapping::picture_point(PointF logical) const noexcept {
    return Point{floor_of(logical.x * x_), floor_of(logical.y * y_)};
}

PointF ViewMapping::logical_point(Point picture) const noexcept { return PointF{picture.x / x_, picture.y / y_}; }

Box ViewMapping::logical_box(const Box& picture) const noexcept {
    const int left = floor_of(picture.x / x_);
    const int top = floor_of(picture.y / y_);
    return Box{left, top, ceil_of(picture.right() / x_) - left, ceil_of(picture.bottom() / y_) - top};
}

Box ViewMapping::logical_box_inside(const Box& picture) const noexcept {
    if (picture.empty()) return {};
    const int left = ceil_of(picture.x / x_);
    const int top = ceil_of(picture.y / y_);
    const int right = floor_of(picture.right() / x_);
    const int bottom = floor_of(picture.bottom() / y_);
    if (right <= left || bottom <= top) return {};
    return Box{left, top, right - left, bottom - top};
}

}  // namespace rwin::host
