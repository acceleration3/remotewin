#pragma once

namespace rwin::host {
struct Point {
    int x = 0;
    int y = 0;

    [[nodiscard]] constexpr bool operator==(const Point&) const noexcept = default;
    [[nodiscard]] constexpr Point operator+(Point other) const noexcept { return Point{x + other.x, y + other.y}; }
    [[nodiscard]] constexpr Point operator-(Point other) const noexcept { return Point{x - other.x, y - other.y}; }
};

}
