#include "proto/sys_menu.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void SysMenu::write(MessageWriter& out) const {
    window.write(out);
    out.put_i32(x);
    out.put_i32(y);
}

std::optional<SysMenu> SysMenu::read(MessageReader& in) {
    SysMenu menu;
    const std::optional<WindowId> window = WindowId::read(in);
    menu.x = in.i32();
    menu.y = in.i32();
    if (!window.has_value() || !in.ok()) return std::nullopt;
    menu.window = *window;
    return menu;
}

}  // namespace rwin
