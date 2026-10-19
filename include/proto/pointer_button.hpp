#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// A button going down or up over a mirror, at a point of its window's
// picture (or the monitor's).
struct PointerButton {
    // Back and Forward are XBUTTON1 and XBUTTON2.
    enum class Button : std::uint8_t { Left = 0, Middle = 1, Right = 2, Back = 3, Forward = 4 };

    [[nodiscard]] static constexpr bool valid(std::uint8_t button) noexcept {
        return button <= static_cast<std::uint8_t>(Button::Forward);
    }

    WindowId window;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Button button = Button::Left;
    bool pressed = false;

    [[nodiscard]] bool operator==(const PointerButton&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerButton> read(MessageReader& in);
};

}  // namespace rwin
