#include "guest/capture/slot_ledger.hpp"

#include <cassert>

namespace rwin::guest {

std::optional<std::uint32_t> SlotLedger::free_slot() const noexcept {
    for (std::uint32_t slot = 0; slot < slots_.size(); ++slot) {
        if (!slots_[slot].hosts) return slot;
    }
    return std::nullopt;
}

std::uint64_t SlotLedger::filled(std::uint32_t slot) {
    assert(slot < slots_.size() && !slots_[slot].hosts);
    Slot& filled = slots_[slot];
    filled.hosts = true;
    return ++filled.copies;
}

bool SlotLedger::freed(std::uint32_t ring, std::uint32_t slot) noexcept {
    if (ring != ring_ || slot >= slots_.size() || !slots_[slot].hosts) return false;
    slots_[slot].hosts = false;
    return true;
}

}  // namespace rwin::guest
