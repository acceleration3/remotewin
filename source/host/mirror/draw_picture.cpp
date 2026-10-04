#include "host/mirror/draw_picture.hpp"

#include "host/mirror/pixel_mapping.hpp"

#include <QImage>
#include <QPainter>

namespace rwin::host {

void draw_picture(QPainter& painter, const QImage& frame, const PixelMapping& where) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !where.one_to_one());
    painter.drawImage(where.picture(frame.size()), frame);
}

}  // namespace rwin::host
