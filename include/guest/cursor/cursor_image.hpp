#pragma once

#include <cstdint>
#include <vector>

namespace rwin::guest {
class CursorImage {
public:
    explicit CursorImage(void* cursor_handle);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t hotspot_x() const noexcept { return hotspot_x_; }
    [[nodiscard]] std::uint32_t hotspot_y() const noexcept { return hotspot_y_; }
    [[nodiscard]] bool inverts() const noexcept { return inverts_; }
    [[nodiscard]] std::vector<std::uint8_t> take_pixels() noexcept;

private:
    bool read_colour(void* device_context, void* colour_bitmap);
    bool read_monochrome(void* device_context, void* mask_bitmap);
    void apply_mask_alpha(void* device_context, void* mask_bitmap);

    std::vector<std::uint8_t> pixels_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t hotspot_x_ = 0;
    std::uint32_t hotspot_y_ = 0;
    bool valid_ = false;
    bool inverts_ = false;
};

}
