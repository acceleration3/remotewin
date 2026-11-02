#pragma once

#include <cstdint>

namespace rwin::guest {
class WindowSurface {
public:
    explicit WindowSurface(std::uint64_t window_handle);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] bool minimized() const noexcept { return minimized_; }
    [[nodiscard]] bool capturable() const noexcept { return valid_ && shown_; }
    [[nodiscard]] bool margin_is_reserved() const noexcept { return margin_is_reserved_; }

    [[nodiscard]] std::int32_t screen_x(std::int32_t picture_x) const noexcept {
        return left_ + picture_x;
    }
    [[nodiscard]] std::int32_t screen_y(std::int32_t picture_y) const noexcept {
        return top_ + picture_y;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    [[nodiscard]] std::int32_t inset_x() const noexcept { return inset_x_; }
    [[nodiscard]] std::int32_t inset_y() const noexcept { return inset_y_; }
    [[nodiscard]] std::uint32_t border_width() const noexcept { return border_width_; }
    [[nodiscard]] std::uint32_t border_height() const noexcept { return border_height_; }

    [[nodiscard]] void* handle() const noexcept { return handle_; }

private:
    void* handle_;
    std::int32_t left_ = 0;
    std::int32_t top_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::int32_t inset_x_ = 0;
    std::int32_t inset_y_ = 0;
    std::uint32_t border_width_ = 0;
    std::uint32_t border_height_ = 0;
    bool minimized_ = false;
    bool shown_ = false;
    bool margin_is_reserved_ = false;
    bool valid_ = false;
};

}
