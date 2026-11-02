#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct FrameInSlot {
    WindowId window;
    std::uint32_t ring = 0;
    std::uint32_t slot = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t copies = 0;
    std::int64_t composed = 0;
    std::int64_t taken = 0;
    std::int64_t copied = 0;

    [[nodiscard]] bool operator==(const FrameInSlot&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<FrameInSlot> read(MessageReader& in);
};

}
