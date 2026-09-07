#pragma once

#include <cstdint>

namespace rwin::guest {

// Pointer and keyboard events delivered to one window.
//
// Posted to the window's own queue rather than synthesised at the desktop
// with SendInput: SendInput goes wherever the foreground is, so two
// mirrored applications would steal each other's keystrokes and the guest
// would have to be given focus it has no screen to show.
class InputInjector {
public:
    enum class Button : std::uint8_t { Left = 0, Middle = 1, Right = 2 };

    void move(std::uint64_t window_handle, std::int32_t x, std::int32_t y) const;
    void button(std::uint64_t window_handle, std::int32_t x, std::int32_t y, Button button,
                bool pressed) const;
    void key(std::uint64_t window_handle, std::uint32_t virtual_key, std::uint32_t character,
             bool pressed) const;
};

}  // namespace rwin::guest
