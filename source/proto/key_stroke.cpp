#include "proto/key_stroke.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void KeyStroke::write(MessageWriter& out) const {
    window.write(out);
    out.put_u16(scan_code);
    out.put_u32(virtual_key);
    out.put_u32(character);
    out.put_u8(pressed ? 1 : 0);
}

std::optional<KeyStroke> KeyStroke::read(MessageReader& in) {
    KeyStroke stroke;
    const std::optional<WindowId> window = WindowId::read(in);
    stroke.scan_code = in.u16();
    stroke.virtual_key = in.u32();
    stroke.character = in.u32();
    stroke.pressed = in.u8() != 0;
    if (!window.has_value() || !in.ok()) return std::nullopt;
    stroke.window = *window;
    return stroke;
}

}  // namespace rwin
