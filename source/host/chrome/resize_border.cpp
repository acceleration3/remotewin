#include "host/chrome/resize_border.hpp"

namespace rwin::host {

void ResizeBorder::fit_to(QMargins band) noexcept {
    band_ = band.isNull() ? QMargins(kThickness, kThickness, kThickness, kThickness) : band;
}

Qt::Edges ResizeBorder::at(QPointF position, QSize size) const noexcept {
    Qt::Edges edges;
    if (position.x() < band_.left()) edges |= Qt::LeftEdge;
    if (position.x() >= size.width() - band_.right()) edges |= Qt::RightEdge;
    if (position.y() < band_.top()) edges |= Qt::TopEdge;
    if (position.y() >= size.height() - band_.bottom()) edges |= Qt::BottomEdge;
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
