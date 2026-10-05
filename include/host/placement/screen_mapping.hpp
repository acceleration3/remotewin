#pragma once

#include <QPoint>
#include <QPointF>

namespace rwin::host {

// Where a point of the guest's screen is on this desktop, and back.
//
// The guest's screen is this desktop's output pixel for pixel: the guest
// is given the output's size and its DPI is set to the output's scale, so
// a guest pixel is a device pixel of the output (as a mirror draws it,
// PixelMapping). A device pixel is 1/scale of a logical unit, and logical
// units are what the compositor places windows in, counted from the
// output's corner in the desktop's layout. At 120% the guest's 152 is 126.67
// logical units in; the compositor keeps that fraction, and 126.67 is 152
// again on the way back.
class ScreenMapping {
public:
    // ``origin`` is the output's corner in the desktop's logical layout;
    // ``scale`` its device pixels per logical unit.
    ScreenMapping(QPointF origin, double scale) noexcept : origin_(origin), scale_(scale) {}

    [[nodiscard]] QPointF host_point(QPoint guest) const noexcept {
        return origin_ + QPointF(guest) / scale_;
    }

    // Device pixels per logical unit.
    [[nodiscard]] double scale() const noexcept { return scale_; }

    // The nearest guest pixel.
    [[nodiscard]] QPoint guest_point(QPointF host) const noexcept {
        return ((host - origin_) * scale_).toPoint();
    }

private:
    QPointF origin_;
    double scale_;
};

}  // namespace rwin::host
