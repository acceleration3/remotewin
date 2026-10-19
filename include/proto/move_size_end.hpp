#pragma once

#include "proto/move_size_kind.hpp"
#include "proto/rect.hpp"
#include "proto/window_id.hpp"

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// Windows' move or resize loop ended (RAIL's Server Move/Size End), with
// the window's frame where it ended: the whole rectangle, not RAIL's
// corner alone.
struct MoveSizeEnd {
    WindowId window;
    MoveSizeKind kind = MoveSizeKind::Move;
    Rect frame;

    [[nodiscard]] bool operator==(const MoveSizeEnd&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<MoveSizeEnd> read(MessageReader& in);
};

}  // namespace rwin
