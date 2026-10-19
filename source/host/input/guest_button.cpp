#include "host/input/guest_button.hpp"

#include "proto/pointer_button.hpp"

#include <linux/input-event-codes.h>

namespace rwin::host {

std::optional<std::uint8_t> guest_button(std::uint32_t code) noexcept {
    using Button = PointerButton::Button;
    switch (code) {
        case BTN_LEFT: return static_cast<std::uint8_t>(Button::Left);
        case BTN_MIDDLE: return static_cast<std::uint8_t>(Button::Middle);
        case BTN_RIGHT: return static_cast<std::uint8_t>(Button::Right);
        case BTN_SIDE:
        case BTN_BACK: return static_cast<std::uint8_t>(Button::Back);
        case BTN_EXTRA:
        case BTN_FORWARD: return static_cast<std::uint8_t>(Button::Forward);
        default: return std::nullopt;
    }
}

}  // namespace rwin::host
