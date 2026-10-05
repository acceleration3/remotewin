#pragma once

#include <Qt>

#include <cstdint>

namespace rwin::host {

// The guest's number for a button of this desktop (PointerInjector::Button
// on the other side): middle 1, right 2, and the left button - and any
// button the guest has no number for - 0.
[[nodiscard]] std::uint8_t guest_button(Qt::MouseButton button) noexcept;

}  // namespace rwin::host
