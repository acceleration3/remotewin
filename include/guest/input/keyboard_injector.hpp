#pragma once

#include <cstdint>

namespace rwin {
struct KeyStroke;
}

namespace rwin::guest {

class WindowFocus;

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
class KeyboardInjector {
public:
    explicit KeyboardInjector(const WindowFocus& focus) noexcept : focus_(focus) {}

    void put(const KeyStroke& stroke) const;

private:
    void type(std::uint32_t character) const;
    void press_scan_code(std::uint16_t scan_code, bool pressed) const;
    void press_virtual_key(std::uint32_t virtual_key, bool pressed) const;

    const WindowFocus& focus_;
};

}  // namespace rwin::guest
