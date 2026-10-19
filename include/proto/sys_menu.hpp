#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// Open the window's own menu (RAIL's Client System Menu) at (x, y),
// relative to the window's frame.
struct SysMenu {
    WindowId window;
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] bool operator==(const SysMenu&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SysMenu> read(MessageReader& in);
};

}  // namespace rwin
