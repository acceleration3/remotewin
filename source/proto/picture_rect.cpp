#include "proto/picture_rect.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void PictureRect::write(MessageWriter& out) const {
    out.put_u32(x);
    out.put_u32(y);
    out.put_u32(width);
    out.put_u32(height);
}

std::optional<PictureRect> PictureRect::read(MessageReader& in) {
    PictureRect rect;
    rect.x = in.u32();
    rect.y = in.u32();
    rect.width = in.u32();
    rect.height = in.u32();
    if (!in.ok()) return std::nullopt;
    return rect;
}

}
