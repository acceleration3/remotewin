#pragma once

#include <cstdint>

namespace rwin::host {

// A key of the seat's keyboard, as the keymap the compositor gave reads it.
struct KeyPress {
    // linux/input-event-codes.h: the XKB keycode less 8.
    std::uint32_t code = 0;
    bool pressed = false;
    // Made by the keyboard's own repeat, at the compositor's rate.
    bool repeat = false;
    // The keysym and the character the keymap makes of it now; 0 for none.
    std::uint32_t keysym = 0;
    std::uint32_t character = 0;
};

}  // namespace rwin::host
