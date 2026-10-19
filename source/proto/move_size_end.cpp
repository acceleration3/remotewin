#include "proto/move_size_end.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void MoveSizeEnd::write(MessageWriter& out) const {
    window.write(out);
    out.put_u8(static_cast<std::uint8_t>(kind));
    frame.write(out);
}

std::optional<MoveSizeEnd> MoveSizeEnd::read(MessageReader& in) {
    MoveSizeEnd end;
    const std::optional<WindowId> window = WindowId::read(in);
    const std::uint8_t kind = in.u8();
    const std::optional<Rect> frame = Rect::read(in);
    if (!window.has_value() || !frame.has_value() || !valid_move_size_kind(kind)) return std::nullopt;
    end.window = *window;
    end.kind = static_cast<MoveSizeKind>(kind);
    end.frame = *frame;
    return end;
}

}  // namespace rwin
