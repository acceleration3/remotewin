#pragma once

#include <cstdint>

namespace rwin::host {

// The Windows scan code (set 1, as KeyStroke carries it: 0xE0 prefix in
// the high byte) of the key Linux numbers ``evdev_code`` - the code in
// linux/input-event-codes.h, which is what a Wayland or X11 keycode is,
// less 8. 0 for a key Windows has no scan code for, and for Pause, whose
// E1 sequence a single keystroke cannot carry.
[[nodiscard]] std::uint16_t windows_scan_code(std::uint32_t evdev_code) noexcept;

}  // namespace rwin::host
