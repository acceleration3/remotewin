#pragma once

#include <Qt>

#include <cstdint>

namespace rwin::host {

// The guest's number for a button of this desktop (PointerButton::Button):
// middle 1, right 2, back 3 and forward 4 (XBUTTON1 and XBUTTON2), and
// the left button - and any button the guest has no number for - 0.
[[nodiscard]] std::uint8_t guest_button(Qt::MouseButton button) noexcept;

}  // namespace rwin::host
