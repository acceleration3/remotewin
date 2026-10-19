#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace rwin {
struct KeyStroke;
struct WindowId;
}

namespace rwin::host {

class GuestLink;

// The keys pressed in a window of this desktop and not yet released there.
//
// A key held when the focus goes elsewhere is released there, not here,
// and the guest would go on holding it - a Shift that sticks. So whoever
// loses the focus releases in the guest what it still holds.
class HeldKeys {
public:
    void note(const KeyStroke& stroke);
    // A release of every key still held, sent for ``window``; none are held
    // after.
    void release_all(GuestLink& link, WindowId window);

private:
    // One bit per key: the set 1 code below kExtendedSlot, an E0 one above.
    static constexpr std::size_t kExtendedSlot = 0x100;
    [[nodiscard]] static std::size_t slot_of(std::uint16_t scan_code) noexcept;

    std::bitset<2 * kExtendedSlot> held_;
};

}  // namespace rwin::host
