#include "host/link/guest_window.hpp"

namespace rwin::host {

namespace {

QRect to_qrect(const Rect& rect) {
    return QRect(rect.x, rect.y, static_cast<int>(rect.width), static_cast<int>(rect.height));
}

}  // namespace

WindowId GuestWindow::owner() const noexcept {
    return fields_.owner.is_hidden_owner() ? WindowId{} : fields_.owner;
}

QRect GuestWindow::frame() const { return to_qrect(fields_.geometry.frame); }

QPoint GuestWindow::picture_origin() const {
    return QPoint(fields_.geometry.frame.x + fields_.geometry.picture.x,
                  fields_.geometry.frame.y + fields_.geometry.picture.y);
}

QSize GuestWindow::picture_size() const {
    return QSize(static_cast<int>(fields_.geometry.picture.width), static_cast<int>(fields_.geometry.picture.height));
}

QSize GuestWindow::picture_padding() const { return frame().size() - picture_size(); }

QMargins GuestWindow::margins() const {
    const WindowFields::Geometry& geometry = fields_.geometry;
    return QMargins(geometry.margin_left, geometry.margin_top, geometry.margin_right, geometry.margin_bottom);
}

QList<QRect> GuestWindow::region() const {
    QList<QRect> region;
    region.reserve(static_cast<qsizetype>(fields_.geometry.region.size()));
    const QPoint picture(fields_.geometry.picture.x, fields_.geometry.picture.y);
    for (const Rect& rect : fields_.geometry.region) {
        if (rect.width == 0 || rect.height == 0) continue;
        region.push_back(to_qrect(rect).translated(-picture));
    }
    return region;
}

QPoint GuestWindow::offset_from_anchor(const GuestWindow& anchor) const {
    const WindowFields::Geometry& own = fields_.geometry;
    const WindowFields::Geometry& theirs = anchor.fields().geometry;
    return QPoint(fields_.popup.rect.x + own.picture.x - theirs.picture.x,
                  fields_.popup.rect.y + own.picture.y - theirs.picture.y);
}

}  // namespace rwin::host
