#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct PointerWheel {
    WindowId window;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;

    [[nodiscard]] bool operator==(const PointerWheel&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerWheel> read(MessageReader& in);
};

}
