#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

// A window's icon (RAIL's Window Icon order, as pixels rather than a DIB and
// its mask): the one Windows' taskbar and title bar show for it, from
// WM_GETICON or its class. Sent when the window is admitted and whenever the
// icon handle changes.
struct WindowIcon {
    enum class Size : std::uint8_t { Small = 0, Big = 1 };

    static constexpr std::uint16_t kMaxSide = 256;

    WindowId window;
    Size size = Size::Big;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    // Straight BGRA, top-down, width * 4 per row.
    std::vector<std::uint8_t> bgra;

    [[nodiscard]] bool operator==(const WindowIcon&) const = default;

    void write(MessageWriter& out) const;
    // Refused for no pixels, a side past kMaxSide, or pixels cut short.
    [[nodiscard]] static std::optional<WindowIcon> read(MessageReader& in);
};

}  // namespace rwin
