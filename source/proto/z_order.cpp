#include "proto/z_order.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

bool ZOrder::above_marker(WindowId window) const noexcept {
    bool seen = false;
    for (const WindowId& listed : windows) {
        if (listed == kMonitorWindow) return seen;
        seen = seen || listed == window;
    }
    return false;
}

void ZOrder::write(MessageWriter& out) const {
    active.write(out);
    out.put_u16(static_cast<std::uint16_t>(windows.size()));
    for (const WindowId& window : windows) window.write(out);
}

std::optional<ZOrder> ZOrder::read(MessageReader& in) {
    ZOrder order;
    const std::optional<WindowId> active = WindowId::read(in);
    const std::uint16_t count = in.u16();
    if (!active.has_value() || !in.ok() || count > kMaxWindows) return std::nullopt;
    order.active = *active;
    order.windows.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::optional<WindowId> window = WindowId::read(in);
        if (!window.has_value()) return std::nullopt;
        order.windows.push_back(*window);
    }
    return order;
}

}  // namespace rwin
