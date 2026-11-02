#include "proto/probe/probe_action.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void ProbeAction::write(MessageWriter& out) const {
    out.put_u8(static_cast<std::uint8_t>(kind));
    out.put_u64(window);
    out.put_u64(other);
    out.put_u8(on ? 1 : 0);
    out.put_i32(show);
    out.put_i32(x);
    out.put_i32(y);
    out.put_i32(width);
    out.put_i32(height);
    out.put_u8(centred ? 1 : 0);
    out.put_u8(static_cast<std::uint8_t>(z));
    out.put_u32(flags);
    out.put_u32(cursor);
    out.put_string(title);
}

std::optional<ProbeAction> ProbeAction::read(MessageReader& in) {
    ProbeAction action;
    const std::uint8_t kind = in.u8();
    action.window = in.u64();
    action.other = in.u64();
    action.on = in.u8() != 0;
    action.show = in.i32();
    action.x = in.i32();
    action.y = in.i32();
    action.width = in.i32();
    action.height = in.i32();
    action.centred = in.u8() != 0;
    const std::uint8_t z = in.u8();
    action.flags = in.u32();
    action.cursor = in.u32();
    action.title = in.string();
    if (!in.ok()) return std::nullopt;
    if (kind > static_cast<std::uint8_t>(Kind::PointerPrecision)) return std::nullopt;
    if (z > static_cast<std::uint8_t>(ZOrder::NoTopMost)) return std::nullopt;
    action.kind = static_cast<Kind>(kind);
    action.z = static_cast<ZOrder>(z);
    return action;
}

}
