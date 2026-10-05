#pragma once

#include <QRect>
#include <QRegion>
#include <QRgb>

#include <optional>

namespace rwin::host {

// How a guest window's picture is to be drawn on a mirror's own pixels:
// what QPainter is told on the pixel path (MirrorWindow::paintEvent,
// draw_picture), said once for a picture drawn on the GPU (GpuPicture).
struct PictureLayout {
    // Where the picture goes, in the mirror's device pixels from its top
    // left: its own size at a fit of 1, which makes the draw a copy.
    QRect picture;
    // Fitted: resampled, rather than copied pixel for pixel.
    bool smooth = false;
    // A layered window's constant alpha, from 0 to 1.
    double opacity = 1.0;
    // A colour-keyed window's key as its pixels hold it (0xRRGGBB); none
    // for any other window.
    std::optional<QRgb> color_key;
    // Where any of the picture may land, in device pixels: a shaped
    // window's region. Empty is everywhere.
    QRegion shape;
    // Whether all of the mirror outside the picture is filled black: the
    // bars beside a fitted picture, which are the mirror's own.
    bool bars = false;

    [[nodiscard]] bool operator==(const PictureLayout&) const = default;
};

}  // namespace rwin::host
