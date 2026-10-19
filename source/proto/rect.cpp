#include "proto/rect.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void Rect::write(MessageWriter& out) const {
    out.put_i32(x);
    out.put_i32(y);
    out.put_u32(width);
    out.put_u32(height);
}

std::optional<Rect> Rect::read(MessageReader& in) {
    Rect rect;
    rect.x = in.i32();
    rect.y = in.i32();
    rect.width = in.u32();
    rect.height = in.u32();
    if (!in.ok()) return std::nullopt;
    return rect;
}

}  // namespace rwin
