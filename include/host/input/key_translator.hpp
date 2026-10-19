#pragma once

#include "proto/key_stroke.hpp"
#include "proto/window_id.hpp"

#include <optional>

namespace rwin::host {

struct KeyPress;

// A key of this desktop's keyboard, as the keystroke the guest is sent.
//
// The key's place on the keyboard (its set 1 scan code, from its evdev
// code) when Windows has one, so the guest's own layout decides what it
// means; its virtual key for one Windows has a name but no single scan code
// for (Pause); the character the keymap makes of it when there is neither.
[[nodiscard]] std::optional<KeyStroke> stroke_for(const KeyPress& press, WindowId window);

}  // namespace rwin::host
