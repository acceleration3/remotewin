#include "proto/guest_cursor.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void GuestCursor::write(MessageWriter& out) const {
    out.put_u64(id);
    out.put_u8(shown ? 1 : 0);
}

std::optional<GuestCursor> GuestCursor::read(MessageReader& in) {
    GuestCursor cursor;
    cursor.id = in.u64();
    cursor.shown = in.u8() != 0;
    if (!in.ok()) return std::nullopt;
    return cursor;
}

}  // namespace rwin
