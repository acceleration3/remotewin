#pragma once

#include "proto/move_size_kind.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct MoveSizeStart {
    WindowId window;
    MoveSizeKind kind = MoveSizeKind::Move;
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] bool operator==(const MoveSizeStart&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<MoveSizeStart> read(MessageReader& in);
};

}
