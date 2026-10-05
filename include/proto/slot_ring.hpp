#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// A window's ring of shared surfaces (SlotRing, guest to host): kSlots
// Direct3D 11 textures the picture's size, which the guest copies its
// frames into, and the id the guest's GPU device knows each by - what the
// host asks the device's export socket for.
//
// Made again, with a new ``ring`` and new surfaces, whenever the picture
// changes size. ``ring`` counts up across the guest half's whole session,
// so a slot the host frees from a ring that has been replaced is never
// taken for the same slot of its replacement.
struct SlotRing {
    // Three: one the host shows, one the guest copies into, one spare, so
    // neither waits on the other in the steady state.
    static constexpr std::size_t kSlots = 3;

    std::uint64_t window = 0;
    std::uint32_t ring = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::array<std::uint64_t, kSlots> surfaces{};

    [[nodiscard]] bool operator==(const SlotRing&) const noexcept = default;

    void write(MessageWriter& out) const;
    // Refused when it names a surface 0, which no shared surface is, or
    // has no pixels.
    [[nodiscard]] static std::optional<SlotRing> read(MessageReader& in);
};

}  // namespace rwin
