#pragma once

#include "proto/slot_ring.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace rwin::guest {

// Which slots of one ring of shared surfaces the guest may copy into, and
// how many copies it has made into each.
//
// A slot is the host's from the copy the guest names to it (FrameInSlot)
// until the host frees it (SlotFree): nothing on the GPU keeps a copy off a
// surface the host is still drawing from, so this is what does. The counts
// are the guest's half of the GPU device's: the device counts every copy
// into a shared surface, so a count here that missed one, or counted one
// that was never made, names a frame the host would wait for forever or
// show before it was there.
class SlotLedger {
public:
    explicit SlotLedger(std::uint32_t ring) noexcept : ring_(ring) {}

    [[nodiscard]] std::uint32_t ring() const noexcept { return ring_; }

    // A slot the host is not reading, or none while it reads every one.
    [[nodiscard]] std::optional<std::uint32_t> free_slot() const noexcept;

    // One copy into ``slot`` was made; the slot is the host's until it frees
    // it. The copy's number, counted from 1, is returned.
    std::uint64_t filled(std::uint32_t slot);

    // The host freed ``slot`` of ``ring``: false, and nothing changed, when
    // that is another ring's slot - one this replaced - or one the host was
    // never given.
    bool freed(std::uint32_t ring, std::uint32_t slot) noexcept;

private:
    struct Slot {
        std::uint64_t copies = 0;
        bool hosts = false;
    };

    std::uint32_t ring_;
    std::array<Slot, SlotRing::kSlots> slots_{};
};

}  // namespace rwin::guest
