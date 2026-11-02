#include "host/input/key_translator.hpp"

#include "host/input/scan_code.hpp"
#include "host/wayland/key_press.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

namespace rwin::host {

namespace {

constexpr std::uint32_t kFirstPrintable = 0x20;

std::uint32_t virtual_key_for(std::uint32_t keysym) {
    switch (keysym) {
        case XKB_KEY_BackSpace: return 0x08;
        case XKB_KEY_Tab: return 0x09;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: return 0x0D;
        case XKB_KEY_Pause: return 0x13;
        case XKB_KEY_Escape: return 0x1B;
        case XKB_KEY_Prior: return 0x21;
        case XKB_KEY_Next: return 0x22;
        case XKB_KEY_End: return 0x23;
        case XKB_KEY_Home: return 0x24;
        case XKB_KEY_Left: return 0x25;
        case XKB_KEY_Up: return 0x26;
        case XKB_KEY_Right: return 0x27;
        case XKB_KEY_Down: return 0x28;
        case XKB_KEY_Insert: return 0x2D;
        case XKB_KEY_Delete: return 0x2E;
        default: return 0;
    }
}

}

std::optional<KeyStroke> stroke_for(const KeyPress& press, WindowId window) {
    KeyStroke stroke;
    stroke.window = window;
    stroke.pressed = press.pressed;
    stroke.scan_code = windows_scan_code(press.code);
    if (stroke.scan_code != 0) return stroke;
    stroke.virtual_key = virtual_key_for(press.keysym);
    if (stroke.virtual_key != 0) return stroke;
    if (press.character < kFirstPrintable) return std::nullopt;
    stroke.character = press.character;
    return stroke;
}

}
