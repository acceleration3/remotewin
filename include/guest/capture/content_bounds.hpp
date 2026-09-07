#pragma once

#include <cstdint>

namespace rwin::guest {

// Where a window's pixels actually are inside its own rectangle.
//
// A window is routinely larger than what it draws. A menu reserves room
// around itself for the shadow the compositor will put there, and that
// room is captured as nothing at all: a per-window capture holds only
// that window, and the shadow belongs to the desktop composition rather
// than to the window. Shown as-is, the reserved band lets whatever is
// behind show through, which reads as a bright frame around a dark menu.
//
// So the window is asked what it drew, by looking. Scanning inward from
// each edge finds the first row and column that is not transparent, which
// for the usual case of a small margin costs a few rows rather than the
// whole image.
class ContentBounds {
public:
    // ``pixels`` is BGRA, top-down, ``width * 4`` per row.
    ContentBounds(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height);

    [[nodiscard]] bool empty() const noexcept { return width_ == 0 || height_ == 0; }
    [[nodiscard]] std::uint32_t left() const noexcept { return left_; }
    [[nodiscard]] std::uint32_t top() const noexcept { return top_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    std::uint32_t left_ = 0;
    std::uint32_t top_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

}  // namespace rwin::guest
