#include "host/cursor/pixel_cursor.hpp"

#include "host/cursor/unit_padding.hpp"

#include <QPainter>
#include <QPixmap>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace rwin::host {

namespace {

constexpr double kExact = 1e-3;

}  // namespace

PixelCursor::PixelCursor(const PointerGlobals& globals) : viewport_(globals) {}

void PixelCursor::put_on(QWidget& widget, const CursorLook::Frame& frame) {
    const double ratio = widget.devicePixelRatioF();
    if (std::optional<QCursor> padded = padded_for(frame, ratio)) {
        widget.setCursor(*padded);
        return;
    }
    widget.setCursor(resampled_for(frame, ratio));
}

// Drawn by Qt at buffer scale 1, its surface sized by the viewport.
std::optional<QCursor> PixelCursor::padded_for(const CursorLook::Frame& frame, double ratio) {
    if (!viewport_.usable()) return std::nullopt;
    const std::optional<UnitPadding> padding = unit_padding(frame.image.size(), frame.hotspot, ratio);
    if (!padding.has_value() || !viewport_.size_next(padding->logical)) return std::nullopt;
    QImage canvas(padding->padded, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(padding->offset, frame.image);
    }
    return QCursor(QPixmap::fromImage(canvas), padding->logical_hotspot.x(), padding->logical_hotspot.y());
}

// At the ratio rounded up, which Qt takes as the buffer scale: resampled by
// that over the ratio, the compositor's scaling back down lands it at its
// own size. The buffer is a whole number of units at that scale, even at a
// whole ratio - one that is not is a protocol error (wl_surface
// invalid_size), which ends the connection.
QCursor PixelCursor::resampled_for(const CursorLook::Frame& frame, double ratio) {
    const double whole = std::ceil(ratio - kExact);
    const int scale = static_cast<int>(whole);
    const QSize units(std::max(1, static_cast<int>(std::lround(frame.image.width() / ratio))),
                      std::max(1, static_cast<int>(std::lround(frame.image.height() / ratio))));
    QImage image = frame.image;
    if (image.size() != units * scale) {
        image = frame.image.scaled(units * scale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(whole);
    return QCursor(pixmap, static_cast<int>(std::lround(frame.hotspot.x() / ratio)),
                   static_cast<int>(std::lround(frame.hotspot.y() / ratio)));
}

}  // namespace rwin::host
