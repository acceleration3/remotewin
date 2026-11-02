#pragma once

#include "proto/slot_ring.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace rwin::guest {
class SlotLedger {
public:
    explicit SlotLedger(std::uint32_t ring) noexcept : ring_(ring) {}

    [[nodiscard]] std::uint32_t ring() const noexcept { return ring_; }

    [[nodiscard]] std::optional<std::uint32_t> free_slot() const noexcept;

    std::uint64_t filled(std::uint32_t slot);

    bool freed(std::uint32_t ring, std::uint32_t slot) noexcept;

private:
    struct Slot {
        std::uint64_t copies = 0;
        bool hosts = false;
    };

    std::uint32_t ring_;
    std::array<Slot, SlotRing::kSlots> slots_{};
};

}
