#pragma once

#include <cstdint>
#include <vector>

namespace rwin::guest {

// One picture of a Windows cursor decoded into pixels the host can show.
//
// Two kinds exist. A colour cursor is a bitmap with an alpha channel, or an
// older one whose transparency is in its mask. A monochrome one is two
// planes, some of whose pixels invert what is behind them
// (MonochromePicture).
class CursorImage {
public:
    explicit CursorImage(void* cursor_handle);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t hotspot_x() const noexcept { return hotspot_x_; }
    [[nodiscard]] std::uint32_t hotspot_y() const noexcept { return hotspot_y_; }
    // Some pixel inverts what is under it, and was drawn as MonochromePicture
    // draws one.
    [[nodiscard]] bool inverts() const noexcept { return inverts_; }
    // Straight BGRA, top-down, width * 4 per row; moved out.
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

}  // namespace rwin::guest
