#include "proto/pointer_delta.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PointerDelta::write(MessageWriter& out) const {
    window.write(out);
    out.put_i32(dx);
    out.put_i32(dy);
}

std::optional<PointerDelta> PointerDelta::read(MessageReader& in) {
    PointerDelta delta;
    const std::optional<WindowId> window = WindowId::read(in);
    delta.dx = in.i32();
    delta.dy = in.i32();
    if (!window.has_value() || !in.ok()) return std::nullopt;
    delta.window = *window;
    return delta;
}

}
