#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>

namespace rwin::host {

// Where a guest window's pixels land in a mirror, and which guest pixel a
// point in the mirror is.
//
// Three spaces meet here. Guest pixels are the window's own. Device pixels
// are the screen's, what the mirror's buffer is made of. Logical units are
// what Qt sizes and places a widget in: device pixels divided by the
// screen's scale. On a desktop scaled to 120% that is 1.2, and a logical
// unit is a fraction of a device pixel that no whole number of units lands
// on exactly.
//
// One guest pixel is ``fit`` device pixels, and ``fit`` is 1 unless the
// mirror is maximised against a screen the guest's own does not match. The
// picture starts at ``origin``, in whole device pixels, so a picture at a
// fit of 1 is copied onto the screen's pixels rather than resampled.
//
// Sizes round so nothing is cropped and nothing loops. A window of N guest
// pixels takes N / ratio logical units rounded up, so its buffer always
// holds the whole picture and the slack is under one unit, transparent, at
// the right and bottom edge. A mirror of L units asks the guest for the
// L * ratio pixels it holds whole, rounded down, which is a size that
// rounds back up to L: a resize asked for never comes back as a different
// one that asks again.
class PixelMapping {
public:
    PixelMapping() = default;
    explicit PixelMapping(double device_pixel_ratio, double fit = 1.0, QPoint origin = {});

    [[nodiscard]] double device_pixel_ratio() const noexcept { return device_pixel_ratio_; }
    [[nodiscard]] bool one_to_one() const noexcept { return fit_ == 1.0; }

    // The fit that shows all of ``guest`` in a mirror of ``logical`` units,
    // at this ratio. Exactly 1 when the guest window at one to one fills
    // the mirror to within the rounding of the mirror's own size; anything
    // else is room the picture is scaled to fill, or too little room for
    // it, and the picture is fitted to the mirror's device pixels.
    [[nodiscard]] double fit_for(QSize guest, QSize logical) const;

    // This mapping with the picture of ``guest`` centred in a mirror of
    // ``logical`` units, rounded down to a whole device pixel. A one to one
    // picture in a mirror sized for it is left at the top left: its slack
    // is under one unit, and half of that is less than a device pixel at
    // any ratio below 2.
    [[nodiscard]] PixelMapping centring(QSize guest, QSize logical) const;

    // The logical size that shows ``guest`` whole, rounded up.
    [[nodiscard]] QSize logical_size(QSize guest) const;
    // The guest size a mirror of ``logical`` units holds whole, rounded
    // down. What the guest is asked for when the user sizes a mirror.
    [[nodiscard]] QSize guest_size(QSize logical) const;
    // The device pixels Qt backs a window of ``logical`` units with.
    [[nodiscard]] QSize device_size(QSize logical) const;

    // Where a guest point lands, in logical units, exactly: a caller
    // placing a widget rounds, and says why that is good enough.
    [[nodiscard]] QPointF logical_point(QPoint guest) const;
    // The guest pixel under a logical point.
    [[nodiscard]] QPoint guest_point(QPointF logical) const;
    // The whole logical units a guest rectangle covers any part of. Rounded
    // outward, so a shape cut to it never cuts into the picture.
    [[nodiscard]] QRect logical_rect(QRect guest) const;
    // The whole logical units that lie entirely within a guest rectangle.
    // Rounded inward, so a pointer kept to it is never over a guest pixel
    // outside the rectangle: ClipCursor's, as a confinement. Empty when
    // the rectangle holds no whole unit.
    [[nodiscard]] QRect logical_rect_inside(QRect guest) const;
    // Where a picture of ``guest`` is drawn, in logical units. At a fit of
    // 1 its device size is the guest size, which is what makes the draw a
    // copy.
    [[nodiscard]] QRectF picture(QSize guest) const;
    // The same, in device pixels: where a picture drawn on the GPU goes.
    [[nodiscard]] QRect device_picture(QSize guest) const;

private:
    [[nodiscard]] QSize drawn_size(QSize guest) const;

    double device_pixel_ratio_ = 1.0;
    // Device pixels per guest pixel.
    double fit_ = 1.0;
    // Where guest pixel (0, 0) is, in device pixels.
    QPoint origin_;
};

}  // namespace rwin::host
