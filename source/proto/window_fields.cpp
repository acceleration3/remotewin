#include "proto/window_fields.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

#include <optional>

namespace rwin {

namespace {

bool valid_show(std::uint8_t show) {
    using Show = WindowFields::Show;
    return show == static_cast<std::uint8_t>(Show::Hidden) || show == static_cast<std::uint8_t>(Show::Minimized)
           || show == static_cast<std::uint8_t>(Show::Maximized) || show == static_cast<std::uint8_t>(Show::Shown);
}

bool read_rect(MessageReader& in, Rect& into) {
    const std::optional<Rect> rect = Rect::read(in);
    if (!rect.has_value()) return false;
    into = *rect;
    return true;
}

bool read_id(MessageReader& in, WindowId& into) {
    const std::optional<WindowId> id = WindowId::read(in);
    if (!id.has_value()) return false;
    into = *id;
    return true;
}

void write_geometry(MessageWriter& out, const WindowFields::Geometry& geometry) {
    geometry.frame.write(out);
    geometry.client.write(out);
    out.put_u16(geometry.margin_left);
    out.put_u16(geometry.margin_top);
    out.put_u16(geometry.margin_right);
    out.put_u16(geometry.margin_bottom);
    out.put_u16(static_cast<std::uint16_t>(geometry.region.size()));
    for (const Rect& rect : geometry.region) rect.write(out);
    geometry.picture.write(out);
}

bool read_geometry(MessageReader& in, WindowFields::Geometry& into) {
    if (!read_rect(in, into.frame) || !read_rect(in, into.client)) return false;
    into.margin_left = in.u16();
    into.margin_top = in.u16();
    into.margin_right = in.u16();
    into.margin_bottom = in.u16();
    const std::uint16_t count = in.u16();
    if (!in.ok() || count > WindowFields::kMaxRegionRects) return false;
    into.region.resize(count);
    for (Rect& rect : into.region) {
        if (!read_rect(in, rect)) return false;
    }
    return read_rect(in, into.picture);
}

void write_popup(MessageWriter& out, const WindowFields::Popup& popup) {
    popup.anchor.write(out);
    out.put_u8(static_cast<std::uint8_t>(popup.kind));
    popup.placed.write(out);
    popup.opens_from.write(out);
    out.put_u8(static_cast<std::uint8_t>(popup.edge));
    out.put_u8(static_cast<std::uint8_t>(popup.gravity));
    out.put_u8(popup.adjust);
}

bool read_popup(MessageReader& in, WindowFields::Popup& into) {
    using Popup = WindowFields::Popup;
    if (!read_id(in, into.anchor)) return false;
    const std::uint8_t kind = in.u8();
    if (!read_rect(in, into.placed) || !read_rect(in, into.opens_from)) return false;
    const std::uint8_t edge = in.u8();
    const std::uint8_t gravity = in.u8();
    into.adjust = in.u8();
    constexpr auto kLastEdge = static_cast<std::uint8_t>(Popup::Edge::BottomRight);
    if (!in.ok() || kind > static_cast<std::uint8_t>(Popup::Kind::Tooltip) || edge > kLastEdge || gravity > kLastEdge) {
        return false;
    }
    into.kind = static_cast<Popup::Kind>(kind);
    into.edge = static_cast<Popup::Edge>(edge);
    into.gravity = static_cast<Popup::Edge>(gravity);
    return true;
}

}  // namespace

std::uint32_t WindowFields::differences(const WindowFields& other) const {
    std::uint32_t mask = 0;
    if (owner != other.owner) mask |= kOwner;
    if (style != other.style) mask |= kStyle;
    if (show != other.show) mask |= kShow;
    if (title != other.title) mask |= kTitle;
    if (geometry != other.geometry) mask |= kGeometry;
    if (taskbar != other.taskbar) mask |= kTaskbar;
    if (z_glue != other.z_glue) mask |= kZGlue;
    if (app_id != other.app_id) mask |= kAppId;
    if (class_name != other.class_name) mask |= kClass;
    if (layering != other.layering) mask |= kLayering;
    if (band != other.band) mask |= kBand;
    if (frame != other.frame) mask |= kFrame;
    if (popup != other.popup) mask |= kPopup;
    if (dpi != other.dpi) mask |= kDpi;
    if (cloaked != other.cloaked) mask |= kCloaked;
    return mask;
}

void WindowFields::write(MessageWriter& out, std::uint32_t mask) const {
    out.put_u32(mask);
    if ((mask & kOwner) != 0) owner.write(out);
    if ((mask & kStyle) != 0) {
        out.put_u32(style.style);
        out.put_u32(style.extended);
        out.put_u32(style.class_style);
    }
    if ((mask & kShow) != 0) out.put_u8(static_cast<std::uint8_t>(show));
    if ((mask & kTitle) != 0) out.put_string(title);
    if ((mask & kGeometry) != 0) write_geometry(out, geometry);
    if ((mask & kTaskbar) != 0) out.put_u8(taskbar);
    if ((mask & kZGlue) != 0) out.put_u8(z_glue ? 1 : 0);
    if ((mask & kAppId) != 0) {
        out.put_string(app_id.aumid);
        out.put_u32(app_id.process_id);
        out.put_string(app_id.image);
    }
    if ((mask & kClass) != 0) out.put_string(class_name);
    if ((mask & kLayering) != 0) {
        out.put_u32(layering.flags);
        out.put_u8(layering.alpha);
        out.put_u32(layering.color_key);
    }
    if ((mask & kBand) != 0) out.put_u32(band);
    if ((mask & kFrame) != 0) out.put_u8(frame);
    if ((mask & kPopup) != 0) write_popup(out, popup);
    if ((mask & kDpi) != 0) out.put_u16(dpi);
    if ((mask & kCloaked) != 0) out.put_u8(cloaked ? 1 : 0);
}

bool WindowFields::read(MessageReader& in, WindowFields& into, std::uint32_t& mask) {
    mask = in.u32();
    if (!in.ok() || (mask & ~kAll) != 0) return false;
    if ((mask & kOwner) != 0 && !read_id(in, into.owner)) return false;
    if ((mask & kStyle) != 0) {
        into.style.style = in.u32();
        into.style.extended = in.u32();
        into.style.class_style = in.u32();
    }
    if ((mask & kShow) != 0) {
        const std::uint8_t show = in.u8();
        if (!in.ok() || !valid_show(show)) return false;
        into.show = static_cast<Show>(show);
    }
    if ((mask & kTitle) != 0) into.title = in.string();
    if ((mask & kGeometry) != 0 && !read_geometry(in, into.geometry)) return false;
    if ((mask & kTaskbar) != 0) into.taskbar = in.u8();
    if ((mask & kZGlue) != 0) into.z_glue = in.u8() != 0;
    if ((mask & kAppId) != 0) {
        into.app_id.aumid = in.string();
        into.app_id.process_id = in.u32();
        into.app_id.image = in.string();
    }
    if ((mask & kClass) != 0) into.class_name = in.string();
    if ((mask & kLayering) != 0) {
        into.layering.flags = in.u32();
        into.layering.alpha = in.u8();
        into.layering.color_key = in.u32();
    }
    if ((mask & kBand) != 0) into.band = in.u32();
    if ((mask & kFrame) != 0) into.frame = in.u8();
    if ((mask & kPopup) != 0 && !read_popup(in, into.popup)) return false;
    if ((mask & kDpi) != 0) into.dpi = in.u16();
    if ((mask & kCloaked) != 0) into.cloaked = in.u8() != 0;
    return in.ok();
}

}  // namespace rwin
