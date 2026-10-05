#include "proto/wire_hello.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void WireHello::write(MessageWriter& out) const { out.put_u32(version); }

std::optional<WireHello> WireHello::read(MessageReader& in) {
    WireHello hello;
    hello.version = in.u32();
    if (!in.ok()) return std::nullopt;
    return hello;
}

}  // namespace rwin
