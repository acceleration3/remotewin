#include "proto/pointer_delta.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PointerDelta::write(MessageWriter& out) const {
    out.put_u64(window);
    out.put_i32(dx);
    out.put_i32(dy);
}

std::optional<PointerDelta> PointerDelta::read(MessageReader& in) {
    PointerDelta delta;
    delta.window = in.u64();
    delta.dx = in.i32();
    delta.dy = in.i32();
    if (!in.ok()) return std::nullopt;
    return delta;
}

}  // namespace rwin
