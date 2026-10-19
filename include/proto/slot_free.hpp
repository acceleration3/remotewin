#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The host has finished reading ``slot`` of ``window``'s ring ``ring``
// (SlotFree, host to guest): every draw that sampled it has run on the
// host's GPU, and it will not be read again until the guest names it in a
// FrameInSlot. The guest copies into a slot only once the host has freed
// it; nothing in the GPU device keeps a copy off a surface being read.
struct SlotFree {
    WindowId window;
    std::uint32_t ring = 0;
    std::uint32_t slot = 0;

    [[nodiscard]] bool operator==(const SlotFree&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SlotFree> read(MessageReader& in);
};

}  // namespace rwin
