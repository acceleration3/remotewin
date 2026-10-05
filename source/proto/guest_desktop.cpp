#include "proto/guest_desktop.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void GuestDesktop::write(MessageWriter& out) const {
    out.put_u32(room_width);
    out.put_u32(room_height);
    out.put_u8(drop_shadows ? 1 : 0);
}

std::optional<GuestDesktop> GuestDesktop::read(MessageReader& in) {
    GuestDesktop desktop;
    desktop.room_width = in.u32();
    desktop.room_height = in.u32();
    desktop.drop_shadows = in.u8() != 0;
    if (!in.ok()) return std::nullopt;
    return desktop;
}

}  // namespace rwin
