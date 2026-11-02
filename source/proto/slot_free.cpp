#include "proto/slot_free.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/slot_ring.hpp"

namespace rwin {

void SlotFree::write(MessageWriter& out) const {
    window.write(out);
    out.put_u32(ring);
    out.put_u32(slot);
}

std::optional<SlotFree> SlotFree::read(MessageReader& in) {
    SlotFree freed;
    const std::optional<WindowId> window = WindowId::read(in);
    freed.ring = in.u32();
    freed.slot = in.u32();
    if (!window.has_value() || !in.ok() || freed.slot >= SlotRing::kSlots) return std::nullopt;
    freed.window = *window;
    return freed;
}

}
