#pragma once

#include "proto/window_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct SlotRing {
    static constexpr std::size_t kSlots = 3;

    WindowId window;
    std::uint32_t ring = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::array<std::uint64_t, kSlots> surfaces{};

    [[nodiscard]] bool operator==(const SlotRing&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SlotRing> read(MessageReader& in);
};

}
