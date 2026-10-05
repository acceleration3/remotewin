#include "guest/input/keyboard_injector.hpp"

#include "guest/windows/window_focus.hpp"
#include "logging/logger.hpp"
#include "proto/key_stroke.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <initializer_list>

namespace rwin::guest {

namespace {

// The scan code, and whether it needs the extended prefix.
//
// Asked of the layout rather than matched against a list of virtual keys:
// MAPVK_VK_TO_VSC_EX answers with the 0xE0 or 0xE1 prefix in the high
// byte for exactly the keys that have one, which is the same question a
// hand-written table of arrow and modifier keys is trying to answer, and
// stays right on a layout the table's author never saw.
struct ScanCode {
    WORD value = 0;
    bool extended = false;
};

ScanCode scan_code_of(std::uint32_t virtual_key) {
    const UINT mapped = ::MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC_EX);
    const UINT prefix = mapped >> 8;
    return {static_cast<WORD>(mapped & 0xFF), prefix == 0xE0 || prefix == 0xE1};
}

UINT send(WORD virtual_key, WORD scan, DWORD flags) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtual_key;
    input.ki.wScan = scan;
    input.ki.dwFlags = flags;
    return ::SendInput(1, &input, sizeof(input));
}

}  // namespace

void KeyboardInjector::put(const KeyStroke& stroke) const {
    focus_.give_to(stroke.window);
    if (stroke.scan_code != 0) {
        press_scan_code(stroke.scan_code, stroke.pressed);
    } else if (stroke.character != 0) {
        if (stroke.pressed) type(stroke.character);
    } else if (stroke.virtual_key != 0) {
        press_virtual_key(stroke.virtual_key, stroke.pressed);
    }
}

void KeyboardInjector::type(std::uint32_t character) const {
    // Anything outside the basic plane arrives as a surrogate pair, and
    // each half goes in as its own keystroke for the receiver to
    // reassemble.
    if (character > 0xFFFF) {
        const std::uint32_t offset = character - 0x10000;
        for (const WORD half : {static_cast<WORD>(0xD800 + (offset >> 10)),
                                static_cast<WORD>(0xDC00 + (offset & 0x3FF))}) {
            send(0, half, KEYEVENTF_UNICODE);
            send(0, half, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP);
        }
        return;
    }
    const auto unit = static_cast<WORD>(character);
    const UINT accepted = send(0, unit, KEYEVENTF_UNICODE)
                          + send(0, unit, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP);
    RWIN_DEBUG("input", "typed {:#x}: {} of 2 events accepted", character, accepted);
}

// wVk is left 0: with KEYEVENTF_SCANCODE Windows ignores it and asks the
// layout, as it does for a key a keyboard sent.
void KeyboardInjector::press_scan_code(std::uint16_t scan_code, bool pressed) const {
    DWORD flags = KEYEVENTF_SCANCODE | (pressed ? 0 : KEYEVENTF_KEYUP);
    if ((scan_code & KeyStroke::kExtended) == KeyStroke::kExtended) {
        flags |= KEYEVENTF_EXTENDEDKEY;
    }
    const UINT accepted = send(0, static_cast<WORD>(scan_code & 0xFF), flags);
    RWIN_DEBUG("input", "scan code {:#06x} {}: {} accepted", scan_code,
               pressed ? "down" : "up", accepted);
}

void KeyboardInjector::press_virtual_key(std::uint32_t virtual_key, bool pressed) const {
    const ScanCode scan = scan_code_of(virtual_key);
    DWORD flags = pressed ? 0 : KEYEVENTF_KEYUP;
    if (scan.extended) flags |= KEYEVENTF_EXTENDEDKEY;
    const UINT accepted = send(static_cast<WORD>(virtual_key), scan.value, flags);
    RWIN_DEBUG("input", "virtual key {:#x} {}: {} accepted", virtual_key,
               pressed ? "down" : "up", accepted);
}

}  // namespace rwin::guest
