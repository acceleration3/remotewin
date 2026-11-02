#pragma once

namespace rwin::host {
struct Size {
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] constexpr bool operator==(const Size&) const noexcept = default;
};

}
