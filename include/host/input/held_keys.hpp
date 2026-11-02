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

class HeldKeys {
public:
    void note(const KeyStroke& stroke);
    void release_all(GuestLink& link, WindowId window);

private:
    static constexpr std::size_t kExtendedSlot = 0x100;
    [[nodiscard]] static std::size_t slot_of(std::uint16_t scan_code) noexcept;

    std::bitset<2 * kExtendedSlot> held_;
};

}
