#pragma once

#include <cstdint>
#include <optional>

namespace rwin::host {

// The guest's number for a button of this desktop (PointerButton::Button),
// from its linux/input-event-codes.h code: left 0, middle 1, right 2, back
// 3 and forward 4 (XBUTTON1 and XBUTTON2) - the side and extra buttons a
// mouse has are what Linux numbers them, and some name them back and
// forward. Nothing for a button Windows has no number for.
[[nodiscard]] std::optional<std::uint8_t> guest_button(std::uint32_t code) noexcept;

}  // namespace rwin::host
