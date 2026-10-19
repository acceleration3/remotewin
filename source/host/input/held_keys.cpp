#include "host/input/held_keys.hpp"

#include "host/link/guest_link.hpp"
#include "proto/key_stroke.hpp"
#include "proto/window_id.hpp"

namespace rwin::host {

std::size_t HeldKeys::slot_of(std::uint16_t scan_code) noexcept {
    return (scan_code & 0xFF) | (((scan_code & KeyStroke::kExtended) != 0) ? kExtendedSlot : 0);
}

void HeldKeys::note(const KeyStroke& stroke) {
    if (stroke.scan_code != 0) held_.set(slot_of(stroke.scan_code), stroke.pressed);
}

void HeldKeys::release_all(GuestLink& link, WindowId window) {
    for (std::size_t slot = 0; slot < held_.size(); ++slot) {
        if (!held_.test(slot)) continue;
        KeyStroke release;
        release.window = window;
        release.scan_code = static_cast<std::uint16_t>(
            (slot & 0xFF) | ((slot & kExtendedSlot) != 0 ? KeyStroke::kExtended : 0));
        link.send_key(release);
    }
    held_.reset();
}

}  // namespace rwin::host
