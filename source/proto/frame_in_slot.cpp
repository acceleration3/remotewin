#include "proto/frame_in_slot.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/slot_ring.hpp"

namespace rwin {

void FrameInSlot::write(MessageWriter& out) const {
    out.put_u64(window);
    out.put_u32(ring);
    out.put_u32(slot);
    out.put_u64(copies);
}

std::optional<FrameInSlot> FrameInSlot::read(MessageReader& in) {
    FrameInSlot frame;
    frame.window = in.u64();
    frame.ring = in.u32();
    frame.slot = in.u32();
    frame.copies = in.u64();
    if (!in.ok() || frame.slot >= SlotRing::kSlots || frame.copies == 0) return std::nullopt;
    return frame;
}

}  // namespace rwin
