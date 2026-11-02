#include "proto/frame_route.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void FrameRoute::write(MessageWriter& out) const { out.put_u8(shared_surfaces ? 1 : 0); }

std::optional<FrameRoute> FrameRoute::read(MessageReader& in) {
    const std::uint8_t shared = in.u8();
    if (!in.ok() || shared > 1) return std::nullopt;
    return FrameRoute{shared == 1};
}

}
