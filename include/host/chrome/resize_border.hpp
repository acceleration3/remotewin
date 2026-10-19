#pragma once

#include <Qt>

#include <QMargins>
#include <QPointF>
#include <QSize>

namespace rwin::host {

// The band around a mirror's edge that starts a resize.
//
// A window's resize border sits OUTSIDE its visible frame - that is
// exactly what the invisible border is - so no point inside the mirror is
// ever in it, and the mirror has no host chrome to grab either, being
// frameless. So the band is inside the mirror's own edges, as wide as the
// guest says the window's borders are, corners winning over sides.
class ResizeBorder {
public:
    // The window's borders, in this mirror's logical units. None at all
    // for a window with a sizing border is a guest that did not say, and
    // the band is the host's own width.
    void fit_to(QMargins band) noexcept;

    [[nodiscard]] Qt::Edges at(QPointF position, QSize size) const noexcept;

    // The pointer shape that says an edge can be dragged. Qt does not
    // infer one for a frameless window.
    [[nodiscard]] static Qt::CursorShape cursor_for(Qt::Edges edges) noexcept;

private:
    // Wide enough to hit without care, narrow enough not to swallow a
    // click meant for a scrollbar sitting against the same edge.
    static constexpr int kThickness = 6;

    QMargins band_{kThickness, kThickness, kThickness, kThickness};
};

}  // namespace rwin::host
