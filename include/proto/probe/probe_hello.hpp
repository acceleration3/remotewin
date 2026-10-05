#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The first thing a probe says: which process it is, so the harness can
// mirror exactly it, and the screen it makes windows on.
struct ProbeHello {
    std::uint32_t process_id = 0;
    // The system DPI; 96 is 100%.
    std::uint32_t dpi = 0;
    std::uint32_t screen_width = 0;
    std::uint32_t screen_height = 0;
    std::uint32_t work_area_width = 0;
    std::uint32_t work_area_height = 0;
    // SPI_GETDROPSHADOW: whether this desktop draws shadows under windows
    // at all - the "Show shadows under windows" performance setting, which
    // CS_DROPSHADOW and DWM's frame shadow both answer to.
    bool drop_shadows = false;

    [[nodiscard]] bool operator==(const ProbeHello&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ProbeHello> read(MessageReader& in);
};

}  // namespace rwin
