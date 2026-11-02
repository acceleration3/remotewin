#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct SlotFree {
    WindowId window;
    std::uint32_t ring = 0;
    std::uint32_t slot = 0;

    [[nodiscard]] bool operator==(const SlotFree&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SlotFree> read(MessageReader& in);
};

}
