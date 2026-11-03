#include "proto/cursor_at.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void CursorAt::write(MessageWriter& out) const {
    out.put_i32(x);
    out.put_i32(y);
}

std::optional<CursorAt> CursorAt::read(MessageReader& in) {
    CursorAt at;
    at.x = in.i32();
    at.y = in.i32();
    if (!in.ok()) return std::nullopt;
    return at;
}

}
