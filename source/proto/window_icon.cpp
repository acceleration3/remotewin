#include "proto/window_icon.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

}  // namespace

void WindowIcon::write(MessageWriter& out) const {
    window.write(out);
    out.put_u8(static_cast<std::uint8_t>(size));
    out.put_u16(width);
    out.put_u16(height);
    out.put_bytes(bgra.data(), bgra.size());
}

std::optional<WindowIcon> WindowIcon::read(MessageReader& in) {
    WindowIcon icon;
    const std::optional<WindowId> window = WindowId::read(in);
    const std::uint8_t size = in.u8();
    icon.width = in.u16();
    icon.height = in.u16();
    if (!window.has_value() || !in.ok() || size > static_cast<std::uint8_t>(Size::Big) || icon.width == 0
        || icon.height == 0 || icon.width > kMaxSide || icon.height > kMaxSide) {
        return std::nullopt;
    }
    icon.window = *window;
    icon.size = static_cast<Size>(size);
    const std::size_t bytes = static_cast<std::size_t>(icon.width) * icon.height * kBytesPerPixel;
    const std::uint8_t* pixels = in.bytes(bytes);
    if (pixels == nullptr || !in.ok()) return std::nullopt;
    icon.bgra.assign(pixels, pixels + bytes);
    return icon;
}

}  // namespace rwin
