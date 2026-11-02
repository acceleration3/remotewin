#pragma once

#include <cstdint>

namespace rwin::guest {
class ContentBounds {
public:
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

}
