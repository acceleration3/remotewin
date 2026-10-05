#include "proto/probe/cursor_truth.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

namespace {

constexpr std::size_t kBytesPerPixel = 4;
// Past any cursor Windows draws, so a malformed answer asks for little.
constexpr std::uint32_t kMaxSide = 1024;
constexpr std::uint32_t kMaxFrames = 512;

void put_pixels(MessageWriter& out, const std::vector<std::uint8_t>& pixels) {
    out.put_bytes(pixels.data(), pixels.size());
}

bool read_pixels(MessageReader& in, std::size_t size, std::vector<std::uint8_t>& pixels) {
    const std::uint8_t* bytes = in.bytes(size);
    if (bytes == nullptr) return false;
    pixels.assign(bytes, bytes + size);
    return true;
}

bool read_frame(MessageReader& in, CursorTruth::Frame& frame) {
    frame.width = in.u32();
    frame.height = in.u32();
    frame.hotspot_x = in.u32();
    frame.hotspot_y = in.u32();
    frame.delay_ms = in.u32();
    if (!in.ok() || frame.width > kMaxSide || frame.height > kMaxSide) return false;
    const std::size_t size = static_cast<std::size_t>(frame.width) * frame.height * kBytesPerPixel;
    return read_pixels(in, size, frame.over_white) && read_pixels(in, size, frame.over_black);
}

}  // namespace

void CursorTruth::write(MessageWriter& out) const {
    out.put_u32(flags);
    out.put_u64(handle);
    out.put_i32(x);
    out.put_i32(y);
    out.put_u64(under);
    out.put_u32(system_idc);
    out.put_i32(display_count);
    out.put_u8(clipped ? 1 : 0);
    out.put_i32(clip.left);
    out.put_i32(clip.top);
    out.put_i32(clip.right);
    out.put_i32(clip.bottom);
    out.put_u64(capture);
    out.put_u8(raw_mouse ? 1 : 0);
    out.put_u32(raw_flags);
    out.put_u64(raw_target);
    out.put_u64(recentring);
    out.put_u64(put_back_by);
    out.put_u32(put_back);
    out.put_i32(mouse_speed);
    out.put_u8(enhance_precision ? 1 : 0);
    out.put_u32(metric_width);
    out.put_u32(metric_height);
    out.put_u32(static_cast<std::uint32_t>(frames.size()));
    for (const Frame& frame : frames) {
        out.put_u32(frame.width);
        out.put_u32(frame.height);
        out.put_u32(frame.hotspot_x);
        out.put_u32(frame.hotspot_y);
        out.put_u32(frame.delay_ms);
        put_pixels(out, frame.over_white);
        put_pixels(out, frame.over_black);
    }
}

std::optional<CursorTruth> CursorTruth::read(MessageReader& in) {
    CursorTruth truth;
    truth.flags = in.u32();
    truth.handle = in.u64();
    truth.x = in.i32();
    truth.y = in.i32();
    truth.under = in.u64();
    truth.system_idc = in.u32();
    truth.display_count = in.i32();
    truth.clipped = in.u8() != 0;
    truth.clip.left = in.i32();
    truth.clip.top = in.i32();
    truth.clip.right = in.i32();
    truth.clip.bottom = in.i32();
    truth.capture = in.u64();
    truth.raw_mouse = in.u8() != 0;
    truth.raw_flags = in.u32();
    truth.raw_target = in.u64();
    truth.recentring = in.u64();
    truth.put_back_by = in.u64();
    truth.put_back = in.u32();
    truth.mouse_speed = in.i32();
    truth.enhance_precision = in.u8() != 0;
    truth.metric_width = in.u32();
    truth.metric_height = in.u32();
    const std::uint32_t count = in.u32();
    if (!in.ok() || count > kMaxFrames) return std::nullopt;
    truth.frames.resize(count);
    for (Frame& frame : truth.frames) {
        if (!read_frame(in, frame)) return std::nullopt;
    }
    if (!in.ok()) return std::nullopt;
    return truth;
}

}  // namespace rwin
