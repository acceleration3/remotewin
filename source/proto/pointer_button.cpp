#include "proto/pointer_button.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PointerButton::write(MessageWriter& out) const {
    window.write(out);
    out.put_i32(x);
    out.put_i32(y);
    out.put_u8(static_cast<std::uint8_t>(button));
    out.put_u8(pressed ? 1 : 0);
}

std::optional<PointerButton> PointerButton::read(MessageReader& in) {
    PointerButton press;
    const std::optional<WindowId> window = WindowId::read(in);
    press.x = in.i32();
    press.y = in.i32();
    const std::uint8_t button = in.u8();
    press.pressed = in.u8() != 0;
    if (!window.has_value() || !in.ok() || !valid(button)) return std::nullopt;
    press.window = *window;
    press.button = static_cast<Button>(button);
    return press;
}

}
