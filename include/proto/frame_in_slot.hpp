#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// A window's newest frame is in ``slot`` of its ring ``ring`` (FrameInSlot,
// guest to host): the ``copies``-th copy the guest made into that slot's
// surface.
//
// A count rather than a moment, because the guest cannot see when its GPU
// device runs the copy. The device counts every copy into a shared surface
// as it runs them, so the host asks it until its count reaches this one,
// and is then handed a fence that signals once the copy is done on the GPU.
//
// ``composed`` and ``copied`` are the guest's performance counter in 100 ns
// units (the clock Windows.Graphics.Capture stamps frames with): the blank
// the compositor composed the frame at, when the guest took it from the
// capture, and when the guest asked for its copy. The
// host puts them on its own clock (GuestClock) to say where a frame's time
// went; 0 is a moment the guest did not see.
struct FrameInSlot {
    std::uint64_t window = 0;
    std::uint32_t ring = 0;
    std::uint32_t slot = 0;
    std::uint64_t copies = 0;
    std::int64_t composed = 0;
    std::int64_t taken = 0;
    std::int64_t copied = 0;

    [[nodiscard]] bool operator==(const FrameInSlot&) const noexcept = default;

    void write(MessageWriter& out) const;
    // Refused when the slot is not one of a ring's, or names no copy.
    [[nodiscard]] static std::optional<FrameInSlot> read(MessageReader& in);
};

}  // namespace rwin
