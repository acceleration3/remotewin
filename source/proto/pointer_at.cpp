#include "proto/pointer_at.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PointerAt::write(MessageWriter& out) const {
    out.put_u64(window);
    out.put_i32(x);
    out.put_i32(y);
    out.put_u8(button);
    out.put_u8(pressed ? 1 : 0);
}

std::optional<PointerAt> PointerAt::read(MessageReader& in) {
    PointerAt at;
    at.window = in.u64();
    at.x = in.i32();
    at.y = in.i32();
    at.button = in.u8();
    at.pressed = in.u8() != 0;
    if (!in.ok()) return std::nullopt;
    return at;
}

}  // namespace rwin
