#include "proto/move_size_start.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void MoveSizeStart::write(MessageWriter& out) const {
    window.write(out);
    out.put_u8(static_cast<std::uint8_t>(kind));
    out.put_i32(x);
    out.put_i32(y);
}

std::optional<MoveSizeStart> MoveSizeStart::read(MessageReader& in) {
    MoveSizeStart start;
    const std::optional<WindowId> window = WindowId::read(in);
    const std::uint8_t kind = in.u8();
    start.x = in.i32();
    start.y = in.i32();
    if (!window.has_value() || !in.ok() || !valid_move_size_kind(kind)) return std::nullopt;
    start.window = *window;
    start.kind = static_cast<MoveSizeKind>(kind);
    return start;
}

}
