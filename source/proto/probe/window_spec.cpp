#include "proto/probe/window_spec.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void WindowSpec::write(MessageWriter& out) const {
    out.put_u32(class_style);
    out.put_u32(style);
    out.put_u32(extended_style);
    out.put_u8(static_cast<std::uint8_t>(layering));
    out.put_u8(alpha);
    out.put_u32(color_key);
    out.put_u8(static_cast<std::uint8_t>(shape));
    out.put_u64(owner);
    out.put_u8(static_cast<std::uint8_t>(show));
    out.put_i32(x);
    out.put_i32(y);
    out.put_u32(client_width);
    out.put_u32(client_height);
    out.put_string(title);
    out.put_i32(corner_preference);
    out.put_i32(dark_mode);
    out.put_i32(nc_rendering_policy);
}

std::optional<WindowSpec> WindowSpec::read(MessageReader& in) {
    WindowSpec spec;
    spec.class_style = in.u32();
    spec.style = in.u32();
    spec.extended_style = in.u32();
    const std::uint8_t layering = in.u8();
    spec.alpha = in.u8();
    spec.color_key = in.u32();
    const std::uint8_t shape = in.u8();
    spec.owner = in.u64();
    const std::uint8_t show = in.u8();
    spec.x = in.i32();
    spec.y = in.i32();
    spec.client_width = in.u32();
    spec.client_height = in.u32();
    spec.title = in.string();
    spec.corner_preference = in.i32();
    spec.dark_mode = in.i32();
    spec.nc_rendering_policy = in.i32();
    if (!in.ok()) return std::nullopt;
    if (layering > static_cast<std::uint8_t>(Layering::AlphaAndColorKey)) return std::nullopt;
    if (shape > static_cast<std::uint8_t>(Shape::Frame)) return std::nullopt;
    if (show > static_cast<std::uint8_t>(ShowState::Hidden)) return std::nullopt;
    spec.layering = static_cast<Layering>(layering);
    spec.shape = static_cast<Shape>(shape);
    spec.show = static_cast<ShowState>(show);
    return spec;
}

}
