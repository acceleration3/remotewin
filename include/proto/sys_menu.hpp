#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct SysMenu {
    WindowId window;
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] bool operator==(const SysMenu&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SysMenu> read(MessageReader& in);
};

}
