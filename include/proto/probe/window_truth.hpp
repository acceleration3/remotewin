#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rwin {

class MessageReader;
class MessageWriter;

struct WindowTruth {
    struct Rect {
        std::int32_t left = 0;
        std::int32_t top = 0;
        std::int32_t right = 0;
        std::int32_t bottom = 0;

        [[nodiscard]] std::int32_t width() const noexcept { return right - left; }
        [[nodiscard]] std::int32_t height() const noexcept { return bottom - top; }
        [[nodiscard]] bool operator==(const Rect&) const noexcept = default;
    };

    enum class RegionKind : std::uint8_t { None, Simple, Complex };

    std::uint64_t handle = 0;
    bool exists = true;
    std::uint64_t owner = 0;
    std::uint32_t process_id = 0;
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    std::uint32_t class_style = 0;
    std::string title;
    std::string class_name;
    Rect window;
    Rect visible;
    Rect client;
    bool shown = false;
    bool iconic = false;
    bool zoomed = false;
    bool enabled = false;
    bool foreground = false;
    bool active = false;
    bool focused = false;
    bool owner_disabled = false;
    std::uint64_t enabled_popup = 0;
    std::int32_t z_rank = -1;
    bool dwm_frame = false;
    std::uint32_t dpi = 0;
    std::uint32_t layered_flags = 0;
    std::uint8_t layered_alpha = 255;
    std::uint32_t color_key = 0;
    RegionKind region = RegionKind::None;
    std::uint32_t region_rects = 0;
    Rect region_box;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<WindowTruth> read(MessageReader& in);
};

}
