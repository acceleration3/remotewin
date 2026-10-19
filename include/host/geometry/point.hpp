#pragma once

namespace rwin::host {

// A point in whole units of the space the caller says.
struct Point {
    int x = 0;
    int y = 0;

    [[nodiscard]] constexpr bool operator==(const Point&) const noexcept = default;
    [[nodiscard]] constexpr Point operator+(Point other) const noexcept { return Point{x + other.x, y + other.y}; }
    [[nodiscard]] constexpr Point operator-(Point other) const noexcept { return Point{x - other.x, y - other.y}; }
};

}  // namespace rwin::host
