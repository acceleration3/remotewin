#pragma once

#include "proto/slot_ring.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace rwin::host {

class SurfaceMemory;

class WindowRing {
public:
    explicit WindowRing(const SlotRing& ring) : ring_(ring) {}

    [[nodiscard]] std::uint32_t ring() const noexcept { return ring_.ring; }
    [[nodiscard]] std::uint64_t surface(std::uint32_t slot) const { return ring_.surfaces.at(slot); }
    [[nodiscard]] const std::array<std::uint64_t, SlotRing::kSlots>& surfaces() const noexcept {
        return ring_.surfaces;
    }

    [[nodiscard]] std::optional<std::uint32_t> slot_of(std::uint64_t surface) const noexcept;

    void exported(std::uint32_t slot, std::shared_ptr<const SurfaceMemory> memory);
    [[nodiscard]] const std::shared_ptr<const SurfaceMemory>& memory(std::uint32_t slot) const {
        return memory_.at(slot);
    }
    void revoke(std::uint32_t slot);
    [[nodiscard]] bool revoked() const noexcept;

    [[nodiscard]] std::uint64_t named() noexcept { return ++named_; }
    [[nodiscard]] bool show(std::uint64_t order) noexcept;

private:
    SlotRing ring_;
    std::array<std::shared_ptr<const SurfaceMemory>, SlotRing::kSlots> memory_{};
    std::array<bool, SlotRing::kSlots> revoked_{};
    std::uint64_t named_ = 0;
    std::uint64_t shown_ = 0;
};

}
