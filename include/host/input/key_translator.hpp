#pragma once

#include "proto/key_stroke.hpp"

#include <QStringView>

#include <cstdint>
#include <optional>

class QKeyEvent;

namespace rwin::host {

// A key event of this desktop, as the keystroke the guest is sent.
//
// The key's place on the keyboard when the platform says what it is, so
// the guest's own layout decides what it means; its text when there is
// only text; its name when there is neither. What a key event's native
// scan code is depends on the platform Qt runs on: on Wayland and X11 it
// is an XKB keycode, the evdev code plus 8, and elsewhere nothing this
// can read.
class KeyTranslator {
public:
    explicit KeyTranslator(QStringView platform_name);

    [[nodiscard]] std::optional<KeyStroke> stroke_for(const QKeyEvent& event,
                                                      std::uint64_t window,
                                                      bool pressed) const;

    // The native scan code this platform reports for the key Linux numbers
    // ``evdev_code``; 0 on a platform that reports none. What a harness
    // puts into a key event to make it the event a keyboard would.
    [[nodiscard]] std::uint32_t native_scan_code(std::uint32_t evdev_code) const noexcept;

private:
    // How far the platform's native scan codes are from evdev's, when it
    // has any.
    std::optional<std::uint32_t> evdev_offset_;
};

}  // namespace rwin::host
