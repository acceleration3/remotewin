#include "host/link/guest_window.hpp"

namespace rwin::host {

namespace {

Box box_of(const Rect& rect) noexcept {
    return Box{rect.x, rect.y, static_cast<int>(rect.width), static_cast<int>(rect.height)};
}

}

WindowId GuestWindow::owner() const noexcept { return fields_.owner.is_hidden_owner() ? WindowId{} : fields_.owner; }

Box GuestWindow::frame() const noexcept { return box_of(fields_.geometry.frame); }

Point GuestWindow::picture_origin() const noexcept { return frame().corner() + picture_offset(); }

Point GuestWindow::picture_offset() const noexcept {
    return Point{fields_.geometry.picture.x, fields_.geometry.picture.y};
}

Size GuestWindow::picture_size() const noexcept {
    return Size{static_cast<int>(fields_.geometry.picture.width), static_cast<int>(fields_.geometry.picture.height)};
}

Size GuestWindow::picture_padding() const noexcept {
    const Box whole = frame();
    const Size picture = picture_size();
    return Size{whole.width - picture.width, whole.height - picture.height};
}

GuestWindow::Margins GuestWindow::margins() const noexcept {
    const WindowFields::Geometry& geometry = fields_.geometry;
    return Margins{geometry.margin_left, geometry.margin_top, geometry.margin_right, geometry.margin_bottom};
}

std::vector<Box> GuestWindow::region() const {
    std::vector<Box> region;
    region.reserve(fields_.geometry.region.size());
    const Point picture = picture_offset();
    for (const Rect& rect : fields_.geometry.region) {
        if (rect.width == 0 || rect.height == 0) continue;
        region.push_back(box_of(rect).translated(Point{-picture.x, -picture.y}));
    }
    return region;
}

std::vector<Box> GuestWindow::opaque_region() const {
    if ((fields_.style.extended & WindowFields::Style::kLayered) != 0) return {};
    return region();
}

}
