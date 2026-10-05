#include "proto/probe/window_truth.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

namespace {

enum StateBit : std::uint16_t {
    kShown = 1,
    kIconic = 2,
    kZoomed = 4,
    kEnabled = 8,
    kForeground = 16,
    kDwmFrame = 32,
    kExists = 64,
    kActive = 128,
    kFocused = 256,
    kOwnerDisabled = 512,
};

std::uint16_t bit(bool set, StateBit which) { return set ? which : 0; }

void put_rect(MessageWriter& out, const WindowTruth::Rect& rect) {
    out.put_i32(rect.left);
    out.put_i32(rect.top);
    out.put_i32(rect.right);
    out.put_i32(rect.bottom);
}

WindowTruth::Rect rect_from(MessageReader& in) {
    WindowTruth::Rect rect;
    rect.left = in.i32();
    rect.top = in.i32();
    rect.right = in.i32();
    rect.bottom = in.i32();
    return rect;
}

}  // namespace

void WindowTruth::write(MessageWriter& out) const {
    out.put_u64(handle);
    out.put_u64(owner);
    out.put_u32(process_id);
    out.put_u32(style);
    out.put_u32(extended_style);
    out.put_u32(class_style);
    out.put_string(title);
    out.put_string(class_name);
    put_rect(out, window);
    put_rect(out, visible);
    put_rect(out, client);
    out.put_u16(static_cast<std::uint16_t>(
        bit(shown, kShown) | bit(iconic, kIconic) | bit(zoomed, kZoomed) | bit(enabled, kEnabled)
        | bit(foreground, kForeground) | bit(dwm_frame, kDwmFrame) | bit(exists, kExists)
        | bit(active, kActive) | bit(focused, kFocused) | bit(owner_disabled, kOwnerDisabled)));
    out.put_u64(enabled_popup);
    out.put_i32(z_rank);
    out.put_u32(dpi);
    out.put_u32(layered_flags);
    out.put_u8(layered_alpha);
    out.put_u32(color_key);
    out.put_u8(static_cast<std::uint8_t>(region));
    out.put_u32(region_rects);
    put_rect(out, region_box);
}

std::optional<WindowTruth> WindowTruth::read(MessageReader& in) {
    WindowTruth truth;
    truth.handle = in.u64();
    truth.owner = in.u64();
    truth.process_id = in.u32();
    truth.style = in.u32();
    truth.extended_style = in.u32();
    truth.class_style = in.u32();
    truth.title = in.string();
    truth.class_name = in.string();
    truth.window = rect_from(in);
    truth.visible = rect_from(in);
    truth.client = rect_from(in);
    const std::uint16_t state = in.u16();
    truth.shown = (state & kShown) != 0;
    truth.iconic = (state & kIconic) != 0;
    truth.zoomed = (state & kZoomed) != 0;
    truth.enabled = (state & kEnabled) != 0;
    truth.foreground = (state & kForeground) != 0;
    truth.dwm_frame = (state & kDwmFrame) != 0;
    truth.exists = (state & kExists) != 0;
    truth.active = (state & kActive) != 0;
    truth.focused = (state & kFocused) != 0;
    truth.owner_disabled = (state & kOwnerDisabled) != 0;
    truth.enabled_popup = in.u64();
    truth.z_rank = in.i32();
    truth.dpi = in.u32();
    truth.layered_flags = in.u32();
    truth.layered_alpha = in.u8();
    truth.color_key = in.u32();
    const std::uint8_t region = in.u8();
    truth.region_rects = in.u32();
    truth.region_box = rect_from(in);
    if (!in.ok() || region > static_cast<std::uint8_t>(RegionKind::Complex)) {
        return std::nullopt;
    }
    truth.region = static_cast<RegionKind>(region);
    return truth;
}

}  // namespace rwin
