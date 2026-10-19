#include "proto/activate.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void Activate::write(MessageWriter& out) const {
    window.write(out);
    out.put_u8(enabled ? 1 : 0);
}

std::optional<Activate> Activate::read(MessageReader& in) {
    const std::optional<WindowId> window = WindowId::read(in);
    const std::uint8_t enabled = in.u8();
    if (!window.has_value() || !in.ok() || enabled > 1) return std::nullopt;
    return Activate{*window, enabled == 1};
}

}  // namespace rwin
