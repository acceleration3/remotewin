#pragma once

#include <cstdint>

namespace rwin::guest {

class WindowFocus;

// Characters and the keys that are not characters.
//
// A character goes in as a Unicode code point rather than as a virtual
// key: the host has already applied its own layout, its own dead keys and
// its own modifiers by the time it has a character, and translating back
// into a keystroke would apply the guest's layout on top of a decision
// already made. It also removes the WM_CHAR versus WM_KEYDOWN ambiguity
// that shows up as every letter arriving twice.
class KeyboardInjector {
public:
    explicit KeyboardInjector(const WindowFocus& focus) noexcept : focus_(focus) {}

    void type(std::uint64_t window_handle, std::uint32_t character) const;
    void press(std::uint64_t window_handle, std::uint32_t virtual_key,
               bool pressed) const;

private:
    const WindowFocus& focus_;
};

}  // namespace rwin::guest
