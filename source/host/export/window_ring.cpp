#include "host/export/window_ring.hpp"

#include "host/export/surface_memory.hpp"

namespace rwin::host {

std::optional<std::uint32_t> WindowRing::slot_of(std::uint64_t surface) const noexcept {
    for (std::uint32_t slot = 0; slot < ring_.surfaces.size(); ++slot) {
        if (ring_.surfaces[slot] == surface) return slot;
    }
    return std::nullopt;
}

void WindowRing::exported(std::uint32_t slot, std::shared_ptr<const SurfaceMemory> memory) {
    memory_.at(slot) = std::move(memory);
}

void WindowRing::revoke(std::uint32_t slot) {
    memory_.at(slot).reset();
    revoked_.at(slot) = true;
}

bool WindowRing::revoked() const noexcept {
    for (const bool gone : revoked_) {
        if (!gone) return false;
    }
    return true;
}

bool WindowRing::show(std::uint64_t order) noexcept {
    if (order <= shown_) return false;
    shown_ = order;
    return true;
}

}
