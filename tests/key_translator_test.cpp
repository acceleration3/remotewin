#include "host/input/key_translator.hpp"
#include "host/input/scan_code.hpp"

#include <QGuiApplication>
#include <QKeyEvent>

#include <linux/input-event-codes.h>

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
using rwin::host::KeyTranslator;
using rwin::host::windows_scan_code;

constexpr rwin::WindowId kWindow{0x500c4, 1};
constexpr std::uint32_t kXkb = 8;

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

QKeyEvent pressed(int key, std::uint32_t evdev, const QString& text,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    return QKeyEvent(QEvent::KeyPress, key, modifiers, evdev == 0 ? 0 : evdev + kXkb, 0, 0, text);
}

void a_physical_key_is_sent_as_its_place() {
    const KeyTranslator wayland{u"wayland"};
    const auto letter = wayland.stroke_for(pressed(Qt::Key_A, KEY_A, QStringLiteral("a")), kWindow, true);
    check(letter.has_value() && letter->scan_code == 0x1E && letter->character == 0
              && letter->virtual_key == 0 && letter->pressed && letter->window == kWindow,
          "a letter is its scan code, not its character");
    const auto shifted = wayland.stroke_for(
        pressed(Qt::Key_Exclam, KEY_1, QStringLiteral("!"), Qt::ShiftModifier), kWindow, true);
    check(shifted.has_value() && shifted->scan_code == 0x02 && shifted->character == 0,
          "Shift+1 is the 1 key: the guest's layout makes the '!'");
    const auto shift = wayland.stroke_for(pressed(Qt::Key_Shift, KEY_RIGHTSHIFT, QString()), kWindow, false);
    check(shift.has_value() && shift->scan_code == 0x36 && !shift->pressed,
          "a modifier alone is a key too, and its release is one");
    check(wayland.native_scan_code(KEY_A) == KEY_A + kXkb, "an XKB keycode is evdev's plus 8");
    check(KeyTranslator{u"xcb"}.native_scan_code(KEY_A) == KEY_A + kXkb, "on X11 as well");
}

void text_without_a_key_is_sent_as_text() {
    const KeyTranslator elsewhere{u"offscreen"};
    check(elsewhere.native_scan_code(KEY_A) == 0, "a platform without XKB codes has none");
    const auto typed = elsewhere.stroke_for(pressed(Qt::Key_A, 0, QStringLiteral("a")), kWindow, true);
    check(typed.has_value() && typed->scan_code == 0 && typed->character == 'a',
          "with no scan code, a letter is its character");
    const auto arrow = elsewhere.stroke_for(pressed(Qt::Key_Left, 0, QString()), kWindow, true);
    check(arrow.has_value() && arrow->virtual_key == 0x25 && arrow->character == 0,
          "and a key with no character its virtual key");
    check(!elsewhere.stroke_for(pressed(Qt::Key_F35, 0, QString()), kWindow, true).has_value(),
          "a key with neither is nothing to send");
    const KeyTranslator wayland{u"wayland"};
    const auto composed = wayland.stroke_for(pressed(Qt::Key_unknown, 0, QStringLiteral("é")), kWindow, true);
    check(composed.has_value() && composed->scan_code == 0 && composed->character == 0xE9,
          "on Wayland, text with no keycode behind it is its character");
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app{argc, argv};
    the_scan_code_table();
    a_physical_key_is_sent_as_its_place();
    text_without_a_key_is_sent_as_text();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("key translator: all checks passed\n");
    return EXIT_SUCCESS;
}
