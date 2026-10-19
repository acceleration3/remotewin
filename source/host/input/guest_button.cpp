#include "host/input/guest_button.hpp"

#include "proto/pointer_button.hpp"

namespace rwin::host {

// Qt::BackButton and Qt::XButton1 are one value, as are ForwardButton and
// XButton2.
std::uint8_t guest_button(Qt::MouseButton button) noexcept {
    using Button = PointerButton::Button;
    switch (button) {
        case Qt::MiddleButton: return static_cast<std::uint8_t>(Button::Middle);
        case Qt::RightButton: return static_cast<std::uint8_t>(Button::Right);
        case Qt::BackButton: return static_cast<std::uint8_t>(Button::Back);
        case Qt::ForwardButton: return static_cast<std::uint8_t>(Button::Forward);
        default: return static_cast<std::uint8_t>(Button::Left);
    }
}

}  // namespace rwin::host
