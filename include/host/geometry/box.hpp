#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"

#include <algorithm>

namespace rwin::host {
struct Box {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] static constexpr Box of(Point corner, Size size) noexcept {
        return Box{corner.x, corner.y, size.width, size.height};
    }

    [[nodiscard]] constexpr Point corner() const noexcept { return Point{x, y}; }
    [[nodiscard]] constexpr Size size() const noexcept { return Size{width, height}; }
    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] constexpr bool contains(Point point) const noexcept {
        return point.x >= x && point.y >= y && point.x < right() && point.y < bottom();
    }
    [[nodiscard]] constexpr bool contains(const Box& other) const noexcept {
        return other.x >= x && other.y >= y && other.right() <= right() && other.bottom() <= bottom();
    }
    [[nodiscard]] constexpr Box translated(Point by) const noexcept { return Box{x + by.x, y + by.y, width, height}; }
    [[nodiscard]] constexpr Box intersected(const Box& other) const noexcept {
        const int left = std::max(x, other.x);
        const int top = std::max(y, other.y);
        return Box{left, top, std::max(0, std::min(right(), other.right()) - left),
                   std::max(0, std::min(bottom(), other.bottom()) - top)};
    }

    [[nodiscard]] constexpr bool operator==(const Box&) const noexcept = default;
};

}
