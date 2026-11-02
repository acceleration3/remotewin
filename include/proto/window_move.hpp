#pragma once

#include "proto/rect.hpp"
#include "proto/window_id.hpp"

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct WindowMove {
    WindowId window;
    Rect frame;

    [[nodiscard]] bool operator==(const WindowMove&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<WindowMove> read(MessageReader& in);
};

}
