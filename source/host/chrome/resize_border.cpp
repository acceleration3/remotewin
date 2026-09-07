#include "host/chrome/resize_border.hpp"

namespace rwin::host {

Qt::Edges ResizeBorder::at(QPointF position, QSize size) const noexcept {
    Qt::Edges edges;
    if (position.x() < kThickness) edges |= Qt::LeftEdge;
    if (position.x() >= size.width() - kThickness) edges |= Qt::RightEdge;
    if (position.y() < kThickness) edges |= Qt::TopEdge;
    if (position.y() >= size.height() - kThickness) edges |= Qt::BottomEdge;
    return edges;
}

Qt::CursorShape ResizeBorder::cursor_for(Qt::Edges edges) noexcept {
    const bool left = edges.testFlag(Qt::LeftEdge);
    const bool right = edges.testFlag(Qt::RightEdge);
    const bool top = edges.testFlag(Qt::TopEdge);
    const bool bottom = edges.testFlag(Qt::BottomEdge);

    if ((left && top) || (right && bottom)) return Qt::SizeFDiagCursor;
    if ((right && top) || (left && bottom)) return Qt::SizeBDiagCursor;
    if (left || right) return Qt::SizeHorCursor;
    if (top || bottom) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

}  // namespace rwin::host
