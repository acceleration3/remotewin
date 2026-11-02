#include "proto/pointer_wheel.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PointerWheel::write(MessageWriter& out) const {
    window.write(out);
    out.put_i32(x);
    out.put_i32(y);
    out.put_i32(dx);
    out.put_i32(dy);
}

std::optional<PointerWheel> PointerWheel::read(MessageReader& in) {
    PointerWheel wheel;
    const std::optional<WindowId> window = WindowId::read(in);
    wheel.x = in.i32();
    wheel.y = in.i32();
    wheel.dx = in.i32();
    wheel.dy = in.i32();
    if (!window.has_value() || !in.ok()) return std::nullopt;
    wheel.window = *window;
    return wheel;
}

}
