#include "proto/window_move.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void WindowMove::write(MessageWriter& out) const {
    window.write(out);
    frame.write(out);
}

std::optional<WindowMove> WindowMove::read(MessageReader& in) {
    const std::optional<WindowId> window = WindowId::read(in);
    const std::optional<Rect> frame = Rect::read(in);
    if (!window.has_value() || !frame.has_value() || frame->width == 0 || frame->height == 0) return std::nullopt;
    return WindowMove{*window, *frame};
}

}
