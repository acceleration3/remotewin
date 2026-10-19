#include "proto/pointer_move.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PointerMove::write(MessageWriter& out) const {
    window.write(out);
    out.put_i32(x);
    out.put_i32(y);
}

std::optional<PointerMove> PointerMove::read(MessageReader& in) {
    PointerMove move;
    const std::optional<WindowId> window = WindowId::read(in);
    move.x = in.i32();
    move.y = in.i32();
    if (!window.has_value() || !in.ok()) return std::nullopt;
    move.window = *window;
    return move;
}

}  // namespace rwin
