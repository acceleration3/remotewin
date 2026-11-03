#include "host/chrome/resize_border.hpp"

namespace rwin::host {

void ResizeBorder::fit_to(int left, int top, int right, int bottom) noexcept {
    const bool none = left == 0 && top == 0 && right == 0 && bottom == 0;
    left_ = none ? kThickness : left;
    top_ = none ? kThickness : top;
    right_ = none ? kThickness : right;
    bottom_ = none ? kThickness : bottom;
}

std::uint32_t ResizeBorder::at(PointF position, Size size) const noexcept {
    std::uint32_t edges = 0;
    if (position.x < left_) edges |= kLeft;
    if (position.x >= size.width - right_) edges |= kRight;
    if (position.y < top_) edges |= kTop;
    if (position.y >= size.height - bottom_) edges |= kBottom;
    if (edges == 0) return edges;
    const bool upper = position.y < size.height / 2.0;
    const bool leftward = position.x < size.width / 2.0;
    if ((edges & (kLeft | kRight)) != 0 && (edges & (kTop | kBottom)) == 0) {
        if (upper && position.y < kCornerReach) edges |= kTop;
        if (!upper && position.y >= size.height - kCornerReach) edges |= kBottom;
    }
    if ((edges & (kTop | kBottom)) != 0 && (edges & (kLeft | kRight)) == 0) {
        if (leftward && position.x < kCornerReach) edges |= kLeft;
        if (!leftward && position.x >= size.width - kCornerReach) edges |= kRight;
    }
    return edges;
}

CursorShape ResizeBorder::cursor_for(std::uint32_t edges) noexcept {
    switch (edges) {
        case kTop | kLeft:
        case kBottom | kRight: return CursorShape::NwseResize;
        case kTop | kRight:
        case kBottom | kLeft: return CursorShape::NeswResize;
        case kLeft:
        case kRight: return CursorShape::EwResize;
        case kTop:
        case kBottom: return CursorShape::NsResize;
        default: return CursorShape::Default;
    }
}

std::optional<std::uint32_t> ResizeBorder::edges_of(MoveSizeKind kind) noexcept {
    switch (kind) {
        case MoveSizeKind::Left: return kLeft;
        case MoveSizeKind::Right: return kRight;
        case MoveSizeKind::Top: return kTop;
        case MoveSizeKind::TopLeft: return kTop | kLeft;
        case MoveSizeKind::TopRight: return kTop | kRight;
        case MoveSizeKind::Bottom: return kBottom;
        case MoveSizeKind::BottomLeft: return kBottom | kLeft;
        case MoveSizeKind::BottomRight: return kBottom | kRight;
        case MoveSizeKind::Move:
        case MoveSizeKind::KeyMove:
        case MoveSizeKind::KeySize: break;
    }
    return std::nullopt;
}

}
