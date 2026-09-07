#pragma once

#include <cstdint>
#include <vector>

namespace rwin::guest {

// A Windows cursor decoded into pixels the host can show.
//
// Two shapes exist and only one of them is an image. A colour cursor is a
// bitmap with an alpha channel. A monochrome one is a single bitmap of
// twice the height: the top half says which pixels are transparent, the
// bottom half says whether the visible ones are black or white — and the
// combination the old API calls XOR means "invert whatever is behind me",
// which no host compositor can be asked to do. Those pixels come out
// opaque black, which is what they look like over the light backgrounds
// they were designed against.
class CursorImage {
public:
    explicit CursorImage(void* cursor_handle);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t hotspot_x() const noexcept { return hotspot_x_; }
    [[nodiscard]] std::uint32_t hotspot_y() const noexcept { return hotspot_y_; }
    // Straight BGRA, top-down, width * 4 per row.
    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept {
        return pixels_;
    }

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
};

}  // namespace rwin::guest
