#include "proto/slot_free.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/slot_ring.hpp"

namespace rwin {

void SlotFree::write(MessageWriter& out) const {
    out.put_u64(window);
    out.put_u32(ring);
    out.put_u32(slot);
}

std::optional<SlotFree> SlotFree::read(MessageReader& in) {
    SlotFree freed;
    freed.window = in.u64();
    freed.ring = in.u32();
    freed.slot = in.u32();
    if (!in.ok() || freed.slot >= SlotRing::kSlots) return std::nullopt;
    return freed;
}

}  // namespace rwin
