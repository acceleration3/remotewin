#include "proto/window_id.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void WindowId::write(MessageWriter& out) const {
    out.put_u32(hwnd);
    out.put_u32(serial);
}

std::optional<WindowId> WindowId::read(MessageReader& in) {
    WindowId id;
    id.hwnd = in.u32();
    id.serial = in.u32();
    if (!in.ok()) return std::nullopt;
    return id;
}

}  // namespace rwin
