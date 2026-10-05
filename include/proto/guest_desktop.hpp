#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// What the guest's desktop is like for every window on it, said at the
// head of each window list: the room a window can have there, and whether
// it shades windows at all.
struct GuestDesktop {
    // The work area: the screen less what the shell reserves on it.
    std::uint32_t room_width = 0;
    std::uint32_t room_height = 0;
    // SPI_GETDROPSHADOW, the "Show shadows under windows" setting. Off, no
    // window is shaded - neither a CS_DROPSHADOW class nor a DWM frame.
    bool drop_shadows = false;

    [[nodiscard]] bool operator==(const GuestDesktop&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<GuestDesktop> read(MessageReader& in);
};

}  // namespace rwin
