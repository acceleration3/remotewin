#pragma once

#include <Qt>

#include <QPointF>
#include <QSize>

namespace rwin::host {

// The band around a mirror's edge that starts a resize.
//
// The guest cannot answer this. A window's resize border sits OUTSIDE
// its visible frame — that is exactly what the invisible border is —
// so no point inside the mirror is ever in it, and asking the guest
// which part of itself the pointer is over never returns an edge. The
// mirror has no host chrome to grab either, being frameless.
//
// So the host draws its own conclusion from the geometry it owns: a few
// pixels in from its own edges, corners winning over sides.
class ResizeBorder {
public:
    [[nodiscard]] Qt::Edges at(QPointF position, QSize size) const noexcept;

    // The pointer shape that says an edge can be dragged. Qt does not
    // infer one for a frameless window.
    [[nodiscard]] static Qt::CursorShape cursor_for(Qt::Edges edges) noexcept;

private:
    // Wide enough to hit without care, narrow enough not to swallow a
    // click meant for a scrollbar sitting against the same edge.
    static constexpr int kThickness = 6;
};

}  // namespace rwin::host
