#pragma once

#include <cstdint>

namespace rwin {
struct KeyStroke;
}

namespace rwin::guest {

// Keys as a keyboard gives them, and text no keyboard key stands for.
//
// A physical key goes in as its scan code. Windows then does what it does
// for a real keyboard: the layout makes it a virtual key - WM_KEYDOWN
// carries VK_A and its scan code, which is what a game polling
// GetAsyncKeyState or an application matching Ctrl+S reads - and
// TranslateMessage makes the character from the guest's own layout and
// modifier state. Injected as a Unicode character instead, every letter
// arrived as VK_PACKET, which nothing that reads keys recognises.
//
// A character goes in as a Unicode code point only when there is no key
// behind it: an input method's composed text.
//
// Keys reach whatever window the guest has in front: the host makes that
// the window it is typing into (Activate) before it types.
void put_key(const KeyStroke& stroke);

}  // namespace rwin::guest
