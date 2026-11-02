#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct PointerDelta {
    WindowId window;
    std::int32_t dx = 0;
    std::int32_t dy = 0;

    [[nodiscard]] bool operator==(const PointerDelta&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerDelta> read(MessageReader& in);
};

}
