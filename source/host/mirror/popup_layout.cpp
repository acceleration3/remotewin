#include "host/mirror/popup_layout.hpp"

#include "host/link/guest_window.hpp"
#include "proto/window_fields.hpp"

#include <algorithm>
#include <cmath>

namespace rwin::host {

namespace {

using Popup = WindowFields::Popup;

constexpr std::uint8_t kSlide = Popup::kSlideX | Popup::kSlideY;

Point rounded(PointF point) {
    return Point{static_cast<int>(std::lround(point.x)), static_cast<int>(std::lround(point.y))};
}

// A frame-relative point of the parent, in its picture's pixels.
Point picture_point_of(const GuestWindow& parent, Point frame_relative) {
    return frame_relative - parent.picture_offset();
}

// Where the popup's picture corner is against the parent's picture: from
// POPUP.placed when the guest said one, else from the two frames.
Point placed_corner(const GuestWindow& popup, const GuestWindow& parent) {
    const WindowFields::Popup& intent = popup.fields().popup;
    const Point frame = intent.anchor == parent.id() ? Point{intent.placed.x, intent.placed.y}
                                                     : popup.frame().corner() - parent.frame().corner();
    return picture_point_of(parent, frame + popup.picture_offset());
}

// A point as an anchor: a rectangle of one unit at it, inside the parent,
// as an anchor has to be - KWin refuses one of no size, which the protocol
// once did too.
Box point_anchor(Point at, Size bounds) {
    return Box{std::clamp(at.x, 0, std::max(0, bounds.width - 1)), std::clamp(at.y, 0, std::max(0, bounds.height - 1)),
               1, 1};
}

PopupPlacement exactly(const GuestWindow& popup, const GuestWindow& parent, const ViewMapping& view, Size size) {
    const Point at = rounded(view.logical_point(placed_corner(popup, parent)));
    const Box anchor = point_anchor(at, view.logical());
    PopupPlacement placement;
    placement.size = size;
    placement.anchor = anchor;
    placement.edge = static_cast<std::uint32_t>(Popup::Edge::TopLeft);
    placement.gravity = static_cast<std::uint32_t>(Popup::Edge::BottomRight);
    placement.adjust = kSlide;
    placement.offset = at - anchor.corner();
    return placement;
}

}  // namespace

PopupPlacement popup_layout(const GuestWindow& popup, const GuestWindow& parent, const ViewMapping& parent_view,
                            Size popup_size) {
    const WindowFields::Popup& intent = popup.fields().popup;
    if (intent.kind == Popup::Kind::Exact || intent.anchor != parent.id()) {
        return exactly(popup, parent, parent_view, popup_size);
    }
    const Point corner =
        picture_point_of(parent, Point{intent.opens_from.x, intent.opens_from.y});
    const Box from = parent_view.logical_box(
        Box{corner.x, corner.y, static_cast<int>(intent.opens_from.width), static_cast<int>(intent.opens_from.height)});
    const Box bounds = Box::of(Point{}, parent_view.logical());
    Box anchor = from.intersected(bounds);
    // A point: the rectangle of no size the guest named.
    if (from.width == 0 || from.height == 0) {
        anchor = point_anchor(from.corner(), bounds.size());
    } else if (anchor.empty()) {
        return exactly(popup, parent, parent_view, popup_size);
    }
    PopupPlacement placement;
    placement.size = popup_size;
    placement.anchor = anchor;
    placement.edge = static_cast<std::uint32_t>(intent.edge);
    placement.gravity = static_cast<std::uint32_t>(intent.gravity);
    placement.adjust = intent.adjust;
    if (intent.kind == Popup::Kind::Tooltip) {
        const PointF placed = parent_view.logical_point(placed_corner(popup, parent));
        const PointF from_point = parent_view.logical_point(corner);
        placement.offset = Point{std::max(0, static_cast<int>(std::lround(placed.x - from_point.x))),
                                 std::max(0, static_cast<int>(std::lround(placed.y - from_point.y)))};
    }
    return placement;
}

Point subsurface_layout(const GuestWindow& part, const GuestWindow& parent, const ViewMapping& parent_view) {
    return rounded(parent_view.logical_point(placed_corner(part, parent)));
}

}  // namespace rwin::host
