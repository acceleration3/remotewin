#include "conform/cursor_picture_on_glass.hpp"

#include <QPainter>

#include <cmath>
#include <utility>

namespace rwin::conform {

namespace {

constexpr double kOneToOne = 1e-6;

QColor background_for(bool over_white) { return over_white ? QColor(Qt::white) : QColor(Qt::black); }

}  // namespace

CursorPictureOnGlass::CursorPictureOnGlass(QImage image, QPoint hotspot, QColor background, double scale)
    : image_(std::move(image)), hotspot_(hotspot), background_(background), scale_(scale) {}

CursorPictureOnGlass CursorPictureOnGlass::of_guest(const CursorTruth::Frame& frame, bool over_white) {
    const std::vector<std::uint8_t>& bgra = over_white ? frame.over_white : frame.over_black;
    const QImage drawn(bgra.data(), static_cast<int>(frame.width), static_cast<int>(frame.height),
                       static_cast<int>(frame.width * 4), QImage::Format_RGB32);
    return CursorPictureOnGlass(drawn.copy(), QPoint(static_cast<int>(frame.hotspot_x), static_cast<int>(frame.hotspot_y)),
                                background_for(over_white), 1.0);
}

std::optional<CursorPictureOnGlass> CursorPictureOnGlass::of_mirror(const host::MirrorState& mirror, bool over_white) {
    if (mirror.cursor_shape != Qt::BitmapCursor || mirror.cursor_pixels.isNull()) return std::nullopt;
    const QImage& pixels = mirror.cursor_pixels;
    const double ratio = mirror.device_pixel_ratio;
    const double scale = mirror.cursor_surface_size.has_value()
                             ? mirror.cursor_surface_size->width() * ratio / pixels.width()
                             : ratio / mirror.cursor_pixmap_ratio;
    QImage shown = pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (std::abs(scale - 1.0) > kOneToOne) {
        shown = shown.scaled(static_cast<int>(std::lround(pixels.width() * scale)),
                             static_cast<int>(std::lround(pixels.height() * scale)), Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    }
    QImage composed(shown.size(), QImage::Format_RGB32);
    composed.fill(background_for(over_white));
    {
        QPainter painter(&composed);
        painter.drawImage(0, 0, shown);
    }
    const QPoint hotspot(static_cast<int>(std::lround(mirror.cursor_hotspot.x() * ratio)),
                         static_cast<int>(std::lround(mirror.cursor_hotspot.y() * ratio)));
    return CursorPictureOnGlass(std::move(composed), hotspot, background_for(over_white), scale);
}

QImage CursorPictureOnGlass::aligned_with(const CursorPictureOnGlass& other) const {
    const QRect mine(-hotspot_, image_.size());
    const QRect theirs(-other.hotspot_, other.image_.size());
    const QRect span = mine | theirs;
    QImage canvas(span.size(), QImage::Format_RGB32);
    canvas.fill(background_);
    QPainter painter(&canvas);
    painter.drawImage(mine.topLeft() - span.topLeft(), image_);
    return canvas;
}

QRect CursorPictureOnGlass::drawn_around_hotspot() const {
    const QRgb background = background_.rgb() & 0xFFFFFF;
    QRect drawn;
    for (int y = 0; y < image_.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image_.constScanLine(y));
        for (int x = 0; x < image_.width(); ++x) {
            if ((row[x] & 0xFFFFFF) != background) drawn |= QRect(x, y, 1, 1);
        }
    }
    return drawn.translated(-hotspot_);
}

}  // namespace rwin::conform
