#pragma once

#include "proto/move_size_kind.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// Windows began moving or resizing a window (RAIL's Server Move/Size
// Start), told after its MinMax. For Move, (x, y) is the pointer's offset
// from the frame's corner; for any other kind, the guest-screen point the
// cursor is at as the loop's event is heard - RAIL's last button-down point,
// as near as the guest half knows it, which has no RDP stack to record one.
// The host reads neither: it hands the gesture to its compositor with the
// serial of its own press. The host takes the gesture over with its own
// compositor and lets the press go, which ends Windows' loop where it
// began: Windows moved nothing.
struct MoveSizeStart {
    WindowId window;
    MoveSizeKind kind = MoveSizeKind::Move;
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] bool operator==(const MoveSizeStart&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<MoveSizeStart> read(MessageReader& in);
};

}  // namespace rwin
