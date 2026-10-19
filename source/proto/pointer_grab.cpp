#include "proto/pointer_grab.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PointerGrab::write(MessageWriter& out) const {
    out.put_u8(static_cast<std::uint8_t>(kind));
    window.write(out);
    out.put_i32(area.left);
    out.put_i32(area.top);
    out.put_i32(area.right);
    out.put_i32(area.bottom);
    out.put_i32(x);
    out.put_i32(y);
}

std::optional<PointerGrab> PointerGrab::read(MessageReader& in) {
    PointerGrab grab;
    const std::uint8_t kind = in.u8();
    const std::optional<WindowId> window = WindowId::read(in);
    grab.area.left = in.i32();
    grab.area.top = in.i32();
    grab.area.right = in.i32();
    grab.area.bottom = in.i32();
    grab.x = in.i32();
    grab.y = in.i32();
    if (!window.has_value() || !in.ok() || kind > static_cast<std::uint8_t>(Kind::Lock)) return std::nullopt;
    grab.window = *window;
    grab.kind = static_cast<Kind>(kind);
    return grab;
}

}  // namespace rwin
