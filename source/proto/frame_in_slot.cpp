#include "proto/frame_in_slot.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/slot_ring.hpp"

namespace rwin {

void FrameInSlot::write(MessageWriter& out) const {
    window.write(out);
    out.put_u32(ring);
    out.put_u32(slot);
    out.put_u32(width);
    out.put_u32(height);
    out.put_u64(copies);
    out.put_u64(static_cast<std::uint64_t>(composed));
    out.put_u64(static_cast<std::uint64_t>(taken));
    out.put_u64(static_cast<std::uint64_t>(copied));
}

std::optional<FrameInSlot> FrameInSlot::read(MessageReader& in) {
    FrameInSlot frame;
    const std::optional<WindowId> window = WindowId::read(in);
    frame.ring = in.u32();
    frame.slot = in.u32();
    frame.width = in.u32();
    frame.height = in.u32();
    frame.copies = in.u64();
    frame.composed = static_cast<std::int64_t>(in.u64());
    frame.taken = static_cast<std::int64_t>(in.u64());
    frame.copied = static_cast<std::int64_t>(in.u64());
    if (!window.has_value() || !in.ok() || frame.slot >= SlotRing::kSlots || frame.copies == 0 || frame.width == 0
        || frame.height == 0) {
        return std::nullopt;
    }
    frame.window = *window;
    return frame;
}

}
