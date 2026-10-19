#include "proto/surface_lost.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void SurfaceLost::write(MessageWriter& out) const {
    window.write(out);
    out.put_u8(static_cast<std::uint8_t>(reason));
}

std::optional<SurfaceLost> SurfaceLost::read(MessageReader& in) {
    const std::optional<WindowId> window = WindowId::read(in);
    const std::uint8_t reason = in.u8();
    if (!window.has_value() || !in.ok() || reason > static_cast<std::uint8_t>(Reason::CaptureFailed)) {
        return std::nullopt;
    }
    return SurfaceLost{*window, static_cast<Reason>(reason)};
}

}  // namespace rwin
