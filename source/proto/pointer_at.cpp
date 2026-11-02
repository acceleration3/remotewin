#include "proto/pointer_at.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/pointer_button.hpp"

namespace rwin {

void PointerAt::write(MessageWriter& out) const {
    window.write(out);
    out.put_i32(x);
    out.put_i32(y);
    out.put_u8(button);
    out.put_u8(pressed ? 1 : 0);
}

std::optional<PointerAt> PointerAt::read(MessageReader& in) {
    PointerAt at;
    const std::optional<WindowId> window = WindowId::read(in);
    at.x = in.i32();
    at.y = in.i32();
    at.button = in.u8();
    at.pressed = in.u8() != 0;
    if (!window.has_value() || !in.ok()) return std::nullopt;
    if (at.button != kNoButton && !PointerButton::valid(at.button)) return std::nullopt;
    at.window = *window;
    return at;
}

}
