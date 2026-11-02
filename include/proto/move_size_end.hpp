#pragma once

#include "proto/move_size_kind.hpp"
#include "proto/rect.hpp"
#include "proto/window_id.hpp"

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct MoveSizeEnd {
    WindowId window;
    MoveSizeKind kind = MoveSizeKind::Move;
    Rect frame;

    [[nodiscard]] bool operator==(const MoveSizeEnd&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<MoveSizeEnd> read(MessageReader& in);
};

}
