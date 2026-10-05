#include "proto/cursor_picture.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

}  // namespace

void CursorPicture::write(MessageWriter& out) const {
    out.put_u64(id);
    out.put_u8(static_cast<std::uint8_t>(system));
    out.put_u8(inverts ? 1 : 0);
    out.put_u32(static_cast<std::uint32_t>(frames.size()));
    for (const Frame& frame : frames) {
        out.put_u32(frame.width);
        out.put_u32(frame.height);
        out.put_u32(frame.hotspot_x);
        out.put_u32(frame.hotspot_y);
        out.put_u32(frame.delay_ms);
        out.put_bytes(frame.bgra.data(), frame.bgra.size());
    }
}

std::optional<CursorPicture> CursorPicture::read(MessageReader& in) {
    CursorPicture picture;
    picture.id = in.u64();
    const std::uint8_t system = in.u8();
    picture.inverts = in.u8() != 0;
    const std::uint32_t count = in.u32();
    if (!in.ok() || count > kMaxFrames || system > static_cast<std::uint8_t>(SystemCursor::Person)) {
        return std::nullopt;
    }
    picture.system = static_cast<SystemCursor>(system);
    picture.frames.resize(count);
    for (Frame& frame : picture.frames) {
        frame.width = in.u32();
        frame.height = in.u32();
        frame.hotspot_x = in.u32();
        frame.hotspot_y = in.u32();
        frame.delay_ms = in.u32();
        if (!in.ok() || frame.width > kMaxSide || frame.height > kMaxSide) return std::nullopt;
        const std::size_t size = static_cast<std::size_t>(frame.width) * frame.height * kBytesPerPixel;
        const std::uint8_t* bytes = in.bytes(size);
        if (bytes == nullptr) return std::nullopt;
        frame.bgra.assign(bytes, bytes + size);
    }
    if (!in.ok()) return std::nullopt;
    return picture;
}

}  // namespace rwin
