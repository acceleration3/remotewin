#include "proto/probe/probe_hello.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void ProbeHello::write(MessageWriter& out) const {
    out.put_u32(process_id);
    out.put_u32(dpi);
    out.put_u32(screen_width);
    out.put_u32(screen_height);
    out.put_u32(work_area_width);
    out.put_u32(work_area_height);
    out.put_u8(drop_shadows ? 1 : 0);
}

std::optional<ProbeHello> ProbeHello::read(MessageReader& in) {
    ProbeHello hello;
    hello.process_id = in.u32();
    hello.dpi = in.u32();
    hello.screen_width = in.u32();
    hello.screen_height = in.u32();
    hello.work_area_width = in.u32();
    hello.work_area_height = in.u32();
    hello.drop_shadows = in.u8() != 0;
    if (!in.ok()) return std::nullopt;
    return hello;
}

}
