#pragma once

#include "proto/slot_ring.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace rwin::host {

class SurfaceMemory;

// What this side knows of one guest window's ring of shared surfaces: the
// surfaces, the memory each was exported as, and which of the window's
// frames is the newest shown.
//
// Frames are named in order but become ready out of it: one whose copy had
// not reached the GPU device when it was asked for is asked again, and the
// next may be ready first. Shown after the next, it would put the window
// back a frame; so a frame is shown only when it is newer than any shown.
class WindowRing {
public:
    explicit WindowRing(const SlotRing& ring) : ring_(ring) {}

    [[nodiscard]] std::uint32_t ring() const noexcept { return ring_.ring; }
    [[nodiscard]] std::uint64_t surface(std::uint32_t slot) const { return ring_.surfaces.at(slot); }
    [[nodiscard]] const std::array<std::uint64_t, SlotRing::kSlots>& surfaces() const noexcept {
        return ring_.surfaces;
    }

    // The slot ``surface`` is, if it is one of this ring's.
    [[nodiscard]] std::optional<std::uint32_t> slot_of(std::uint64_t surface) const noexcept;

    void exported(std::uint32_t slot, std::shared_ptr<const SurfaceMemory> memory);
    // Null until the slot's surface is exported, and again once revoked.
    [[nodiscard]] const std::shared_ptr<const SurfaceMemory>& memory(std::uint32_t slot) const {
        return memory_.at(slot);
    }
    // The guest let go of the surface: nothing is shown from it again.
    void revoke(std::uint32_t slot);
    // Every surface of the ring let go of: the guest is done with the ring,
    // and its ids may name other surfaces from now on.
    [[nodiscard]] bool revoked() const noexcept;

    // A frame was named: its place in the order frames were.
    [[nodiscard]] std::uint64_t named() noexcept { return ++named_; }
    // The frame named ``order``-th is ready: true, and it is the newest
    // shown, when it is newer than every frame shown so far.
    [[nodiscard]] bool show(std::uint64_t order) noexcept;

private:
    SlotRing ring_;
    std::array<std::shared_ptr<const SurfaceMemory>, SlotRing::kSlots> memory_{};
    std::array<bool, SlotRing::kSlots> revoked_{};
    std::uint64_t named_ = 0;
    std::uint64_t shown_ = 0;
};

}  // namespace rwin::host
