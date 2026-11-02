#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct PointerAt {
    static constexpr std::uint8_t kNoButton = 0xFF;

    WindowId window;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint8_t button = kNoButton;
    bool pressed = false;

    [[nodiscard]] bool operator==(const PointerAt&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerAt> read(MessageReader& in);
};

}
