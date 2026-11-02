#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct KeyStroke {
    static constexpr std::uint16_t kExtended = 0xE000;

    WindowId window;
    std::uint16_t scan_code = 0;
    std::uint32_t virtual_key = 0;
    std::uint32_t character = 0;
    bool pressed = false;

    [[nodiscard]] bool extended() const noexcept { return (scan_code & kExtended) == kExtended; }
    [[nodiscard]] std::uint8_t code() const noexcept {
        return static_cast<std::uint8_t>(scan_code & 0xFF);
    }

    [[nodiscard]] bool operator==(const KeyStroke&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<KeyStroke> read(MessageReader& in);
};

}
