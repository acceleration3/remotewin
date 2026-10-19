#include "proto/slot_ring.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

#include <algorithm>

namespace rwin {

void SlotRing::write(MessageWriter& out) const {
    window.write(out);
    out.put_u32(ring);
    out.put_u32(width);
    out.put_u32(height);
    for (const std::uint64_t surface : surfaces) out.put_u64(surface);
}

std::optional<SlotRing> SlotRing::read(MessageReader& in) {
    SlotRing made;
    const std::optional<WindowId> window = WindowId::read(in);
    made.ring = in.u32();
    made.width = in.u32();
    made.height = in.u32();
    for (std::uint64_t& surface : made.surfaces) surface = in.u64();
    if (!window.has_value() || !in.ok() || made.width == 0 || made.height == 0) return std::nullopt;
    made.window = *window;
    if (std::find(made.surfaces.begin(), made.surfaces.end(), 0) != made.surfaces.end()) return std::nullopt;
    return made;
}

}  // namespace rwin
