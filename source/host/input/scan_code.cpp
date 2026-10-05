#include "host/input/scan_code.hpp"

#include <linux/input-event-codes.h>

#include <array>

namespace rwin::host {

namespace {

constexpr std::uint16_t kE0 = 0xE000;

struct Mapping {
    std::uint16_t evdev;
    std::uint16_t windows;
};

// Linux numbered the keys of the PC keyboard by their XT scan codes, so
// from Escape (1) to F12 (88) the two agree code for code; past that, and
// for every key with an 0xE0 prefix, they do not, and each is listed.
// Sources: linux/input-event-codes.h, and Microsoft's "Keyboard Scan Code
// Specification" (USB HID usage to set 1).
constexpr std::array kBeyondXt{
    Mapping{KEY_RO, 0x73},
    Mapping{KEY_KATAKANA, 0x78},
    Mapping{KEY_HIRAGANA, 0x77},
    Mapping{KEY_HENKAN, 0x79},
    Mapping{KEY_KATAKANAHIRAGANA, 0x70},
    Mapping{KEY_MUHENKAN, 0x7B},
    Mapping{KEY_KPJPCOMMA, 0x5C},
    Mapping{KEY_KPENTER, kE0 | 0x1C},
    Mapping{KEY_RIGHTCTRL, kE0 | 0x1D},
    Mapping{KEY_KPSLASH, kE0 | 0x35},
    Mapping{KEY_SYSRQ, kE0 | 0x37},
    Mapping{KEY_RIGHTALT, kE0 | 0x38},
    Mapping{KEY_HOME, kE0 | 0x47},
    Mapping{KEY_UP, kE0 | 0x48},
    Mapping{KEY_PAGEUP, kE0 | 0x49},
    Mapping{KEY_LEFT, kE0 | 0x4B},
    Mapping{KEY_RIGHT, kE0 | 0x4D},
    Mapping{KEY_END, kE0 | 0x4F},
    Mapping{KEY_DOWN, kE0 | 0x50},
    Mapping{KEY_PAGEDOWN, kE0 | 0x51},
    Mapping{KEY_INSERT, kE0 | 0x52},
    Mapping{KEY_DELETE, kE0 | 0x53},
    Mapping{KEY_MUTE, kE0 | 0x20},
    Mapping{KEY_VOLUMEDOWN, kE0 | 0x2E},
    Mapping{KEY_VOLUMEUP, kE0 | 0x30},
    Mapping{KEY_POWER, kE0 | 0x5E},
    Mapping{KEY_KPEQUAL, 0x59},
    Mapping{KEY_KPCOMMA, 0x7E},
    Mapping{KEY_HANGEUL, 0x72},
    Mapping{KEY_HANJA, 0x71},
    Mapping{KEY_YEN, 0x7D},
    Mapping{KEY_LEFTMETA, kE0 | 0x5B},
    Mapping{KEY_RIGHTMETA, kE0 | 0x5C},
    Mapping{KEY_COMPOSE, kE0 | 0x5D},
    Mapping{KEY_MENU, kE0 | 0x5D},
    Mapping{KEY_STOP, kE0 | 0x68},
    Mapping{KEY_CALC, kE0 | 0x21},
    Mapping{KEY_SLEEP, kE0 | 0x5F},
    Mapping{KEY_WAKEUP, kE0 | 0x63},
    Mapping{KEY_MAIL, kE0 | 0x6C},
    Mapping{KEY_BOOKMARKS, kE0 | 0x66},
    Mapping{KEY_COMPUTER, kE0 | 0x6B},
    Mapping{KEY_BACK, kE0 | 0x6A},
    Mapping{KEY_FORWARD, kE0 | 0x69},
    Mapping{KEY_NEXTSONG, kE0 | 0x19},
    Mapping{KEY_PLAYPAUSE, kE0 | 0x22},
    Mapping{KEY_PREVIOUSSONG, kE0 | 0x10},
    Mapping{KEY_STOPCD, kE0 | 0x24},
    Mapping{KEY_HOMEPAGE, kE0 | 0x32},
    Mapping{KEY_REFRESH, kE0 | 0x67},
    Mapping{KEY_F13, 0x64},
    Mapping{KEY_F14, 0x65},
    Mapping{KEY_F15, 0x66},
    Mapping{KEY_F16, 0x67},
    Mapping{KEY_F17, 0x68},
    Mapping{KEY_F18, 0x69},
    Mapping{KEY_F19, 0x6A},
    Mapping{KEY_F20, 0x6B},
    Mapping{KEY_F21, 0x6C},
    Mapping{KEY_F22, 0x6D},
    Mapping{KEY_F23, 0x6E},
    Mapping{KEY_F24, 0x76},
    Mapping{KEY_SEARCH, kE0 | 0x65},
    Mapping{KEY_MEDIA, kE0 | 0x6D},
};

// Inside the XT range: the one code Linux left unassigned, and one it gave
// to a key the XT keyboard did not have.
constexpr std::uint32_t kUnassigned = 84;

bool same_as_xt(std::uint32_t evdev_code) {
    return evdev_code >= KEY_ESC && evdev_code <= KEY_F12 && evdev_code != kUnassigned
           && evdev_code != KEY_ZENKAKUHANKAKU;
}

}  // namespace

std::uint16_t windows_scan_code(std::uint32_t evdev_code) noexcept {
    if (same_as_xt(evdev_code)) return static_cast<std::uint16_t>(evdev_code);
    for (const Mapping& mapping : kBeyondXt) {
        if (mapping.evdev == evdev_code) return mapping.windows;
    }
    return 0;
}

}  // namespace rwin::host
