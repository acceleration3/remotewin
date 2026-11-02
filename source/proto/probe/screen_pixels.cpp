#include "proto/probe/screen_pixels.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

namespace {
constexpr std::size_t kBytesPerPixel = 4;
}

void ScreenPixels::write(MessageWriter& out) const {
    out.put_i32(left);
    out.put_i32(top);
    out.put_u32(width);
    out.put_u32(height);
    out.put_bytes(bgra.data(), bgra.size());
}

std::optional<ScreenPixels> ScreenPixels::read(MessageReader& in) {
    ScreenPixels pixels;
    pixels.left = in.i32();
    pixels.top = in.i32();
    pixels.width = in.u32();
    pixels.height = in.u32();
    const std::size_t size = static_cast<std::size_t>(pixels.width) * pixels.height
                             * kBytesPerPixel;
    const std::uint8_t* bytes = in.bytes(size);
    if (!in.ok() || bytes == nullptr) return std::nullopt;
    pixels.bgra.assign(bytes, bytes + size);
    return pixels;
}

}
