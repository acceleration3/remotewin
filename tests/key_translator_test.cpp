#include "host/input/key_translator.hpp"
#include "host/input/scan_code.hpp"
#include "host/wayland/key_press.hpp"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::KeyStroke;
using rwin::host::KeyPress;
using rwin::host::stroke_for;
using rwin::host::windows_scan_code;

constexpr rwin::WindowId kWindow{0x500c4, 1};

// Every key a PC keyboard has where Windows' set 1 puts it; spot checks
// of each part of the table, by the key's name on both sides.
void the_scan_code_table() {
    check(windows_scan_code(KEY_ESC) == 0x01, "Escape is 01");
    check(windows_scan_code(KEY_A) == 0x1E && windows_scan_code(KEY_Z) == 0x2C, "letters");
    check(windows_scan_code(KEY_1) == 0x02 && windows_scan_code(KEY_0) == 0x0B, "digits");
    check(windows_scan_code(KEY_GRAVE) == 0x29 && windows_scan_code(KEY_SLASH) == 0x35
              && windows_scan_code(KEY_102ND) == 0x56,
          "punctuation, and the key beside a short left Shift");
    check(windows_scan_code(KEY_F1) == 0x3B && windows_scan_code(KEY_F10) == 0x44
              && windows_scan_code(KEY_F11) == 0x57 && windows_scan_code(KEY_F12) == 0x58,
          "F1 to F12, which are not in one run");
    check(windows_scan_code(KEY_F13) == 0x64 && windows_scan_code(KEY_F23) == 0x6E
              && windows_scan_code(KEY_F24) == 0x76,
          "F13 to F24");
    check(windows_scan_code(KEY_LEFTSHIFT) == 0x2A && windows_scan_code(KEY_RIGHTSHIFT) == 0x36,
          "both Shifts, neither extended");
    check(windows_scan_code(KEY_LEFTCTRL) == 0x1D && windows_scan_code(KEY_RIGHTCTRL) == 0xE01D,
          "the right Ctrl is the left one with E0");
    check(windows_scan_code(KEY_LEFTALT) == 0x38 && windows_scan_code(KEY_RIGHTALT) == 0xE038,
          "the right Alt is the left one with E0");
    check(windows_scan_code(KEY_LEFTMETA) == 0xE05B && windows_scan_code(KEY_RIGHTMETA) == 0xE05C
              && windows_scan_code(KEY_COMPOSE) == 0xE05D,
          "the Windows keys and the menu key");
    check(windows_scan_code(KEY_UP) == 0xE048 && windows_scan_code(KEY_INSERT) == 0xE052
              && windows_scan_code(KEY_PAGEDOWN) == 0xE051,
          "the navigation block is E0");
    check(windows_scan_code(KEY_KP7) == 0x47 && windows_scan_code(KEY_KPDOT) == 0x53
              && windows_scan_code(KEY_KPPLUS) == 0x4E && windows_scan_code(KEY_KPASTERISK) == 0x37,
          "the keypad shares codes with the navigation block, without E0");
    check(windows_scan_code(KEY_KPENTER) == 0xE01C && windows_scan_code(KEY_KPSLASH) == 0xE035,
          "keypad Enter and / are E0");
    check(windows_scan_code(KEY_SYSRQ) == 0xE037, "Print Screen");
    check(windows_scan_code(KEY_MUTE) == 0xE020 && windows_scan_code(KEY_PLAYPAUSE) == 0xE022
              && windows_scan_code(KEY_VOLUMEUP) == 0xE030,
          "media keys");
    check(windows_scan_code(KEY_PAUSE) == 0, "Pause has no single scan code");
    check(windows_scan_code(84) == 0 && windows_scan_code(KEY_RESERVED) == 0,
          "codes no key has are no key");
}

KeyPress pressed(std::uint32_t code, std::uint32_t keysym, std::uint32_t character, bool down = true) {
    return KeyPress{code, down, false, keysym, character};
}

void a_physical_key_is_sent_as_its_place() {
    const auto letter = stroke_for(pressed(KEY_A, XKB_KEY_a, 'a'), kWindow);
    check(letter.has_value() && letter->scan_code == 0x1E && letter->character == 0 && letter->virtual_key == 0
              && letter->pressed && letter->window == kWindow,
          "a letter is its scan code, not its character");
    const auto shifted = stroke_for(pressed(KEY_1, XKB_KEY_exclam, '!'), kWindow);
    check(shifted.has_value() && shifted->scan_code == 0x02 && shifted->character == 0,
          "Shift+1 is the 1 key: the guest's layout makes the '!'");
    const auto shift = stroke_for(pressed(KEY_RIGHTSHIFT, XKB_KEY_Shift_R, 0, false), kWindow);
    check(shift.has_value() && shift->scan_code == 0x36 && !shift->pressed,
          "a modifier alone is a key too, and its release is one");
}

void a_key_without_a_scan_code_is_its_name_or_its_text() {
    const auto pause = stroke_for(pressed(KEY_PAUSE, XKB_KEY_Pause, 0), kWindow);
    check(pause.has_value() && pause->scan_code == 0 && pause->virtual_key == 0x13,
          "Pause, whose E1 sequence no keystroke carries, is VK_PAUSE");
    const auto composed = stroke_for(pressed(KEY_RESERVED, XKB_KEY_eacute, 0xE9), kWindow);
    check(composed.has_value() && composed->scan_code == 0 && composed->character == 0xE9,
          "text with no key behind it is its character");
    check(!stroke_for(pressed(KEY_RESERVED, 0, 0), kWindow).has_value(), "a key with neither is nothing to send");
}

}  // namespace

int main() {
    the_scan_code_table();
    a_physical_key_is_sent_as_its_place();
    a_key_without_a_scan_code_is_its_name_or_its_text();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("key translator: all checks passed\n");
    return EXIT_SUCCESS;
}
