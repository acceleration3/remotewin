#pragma once

#include "proto/rect.hpp"
#include "proto/window_id.hpp"

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// Where the host put a window (RAIL's Client Window Move): its frame, on
// the guest's screen, as the host's compositor left the mirror after the
// user moved or resized it. The guest moves and sizes the window there,
// within what its work area allows, without activating it.
struct WindowMove {
    WindowId window;
    Rect frame;

    [[nodiscard]] bool operator==(const WindowMove&) const noexcept = default;

    void write(MessageWriter& out) const;
    // Refused for a frame of no size.
    [[nodiscard]] static std::optional<WindowMove> read(MessageReader& in);
};

}  // namespace rwin
