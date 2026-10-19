#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// One key going down or up in a mirror, for the guest window it mirrors.
//
// A key the host knows as a physical key is sent as that key: its scan
// code in Windows' set 1, which the guest puts in as a keystroke and its
// own layout makes a virtual key and a character of - the virtual key
// games and shortcuts read, the character text fields read. Text with no
// physical key behind it (an input method's) is sent as the character
// alone, and a key the host has only a name for as a virtual key.
struct KeyStroke {
    // The 0xE0 prefix of an extended key, in the high byte of scan_code.
    static constexpr std::uint16_t kExtended = 0xE000;

    WindowId window;
    // Set 1, low byte the code, kExtended set for an E0 key; 0 for none.
    std::uint16_t scan_code = 0;
    // VK_*; 0 for none.
    std::uint32_t virtual_key = 0;
    // A UTF-32 code point; 0 for none.
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

}  // namespace rwin
