#include "host/mirror/pixel_mapping.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace rwin::host {

namespace {

// Within this of a whole number is that number. A fractional scale arrives
// in 120ths (wp_fractional_scale_v1), so a size times one that is not whole
// misses a whole number by at least 1/120 - far more than the error a
// double picks up on the way. 834 * 1.2 is 1000.8, and stays that; 5 * 1.2
// is 6, and a floor that saw 5.9999999 would ask the guest for a pixel less.
constexpr double kWhole = 1e-6;

int floor_of(double value) { return static_cast<int>(std::floor(value + kWhole)); }

int ceil_of(double value) { return static_cast<int>(std::ceil(value - kWhole)); }

int nearest(double value) { return static_cast<int>(std::lround(value)); }

}  // namespace

PixelMapping::PixelMapping(double device_pixel_ratio, double fit, QPoint origin)
    : device_pixel_ratio_(device_pixel_ratio), fit_(fit), origin_(origin) {
    assert(device_pixel_ratio > 0.0 && fit > 0.0);
}

double PixelMapping::fit_for(QSize guest, QSize logical) const {
    if (guest.isEmpty() || logical.isEmpty()) return 1.0;
    const QSize natural = PixelMapping{device_pixel_ratio_}.logical_size(guest);
    const bool fits =
        natural.width() <= logical.width() && natural.height() <= logical.height();
    const bool filled =
        natural.width() == logical.width() || natural.height() == logical.height();
    if (fits && filled) return 1.0;
    const QSize device = device_size(logical);
    return std::min(static_cast<double>(device.width()) / guest.width(),
                    static_cast<double>(device.height()) / guest.height());
}

PixelMapping PixelMapping::centring(QSize guest, QSize logical) const {
    const QSize spare = device_size(logical) - drawn_size(guest);
    // Floored rather than halved toward zero: a picture larger than its
    // mirror, while the guest catches up with a resize, overhangs both
    // sides alike.
    const QPoint origin(static_cast<int>(std::floor(spare.width() / 2.0)),
                        static_cast<int>(std::floor(spare.height() / 2.0)));
    return PixelMapping{device_pixel_ratio_, fit_, origin};
}

QSize PixelMapping::logical_size(QSize guest) const {
    const double units = fit_ / device_pixel_ratio_;
    return QSize(ceil_of(guest.width() * units), ceil_of(guest.height() * units));
}

QSize PixelMapping::guest_size(QSize logical) const {
    const double pixels = device_pixel_ratio_ / fit_;
    return QSize(floor_of(logical.width() * pixels), floor_of(logical.height() * pixels));
}

// Qt multiplies and rounds to the nearest pixel (QSize * qreal), and the
// compositor is handed a buffer that size.
QSize PixelMapping::device_size(QSize logical) const {
    return QSize(nearest(logical.width() * device_pixel_ratio_),
                 nearest(logical.height() * device_pixel_ratio_));
}

QPointF PixelMapping::logical_point(QPoint guest) const {
    return QPointF((origin_.x() + guest.x() * fit_) / device_pixel_ratio_,
                   (origin_.y() + guest.y() * fit_) / device_pixel_ratio_);
}

QPoint PixelMapping::guest_point(QPointF logical) const {
    return QPoint(floor_of((logical.x() * device_pixel_ratio_ - origin_.x()) / fit_),
                  floor_of((logical.y() * device_pixel_ratio_ - origin_.y()) / fit_));
}

QRect PixelMapping::logical_rect(QRect guest) const {
    const QPointF from = logical_point(guest.topLeft());
    const QPointF to = logical_point(guest.topLeft() + QPoint(guest.width(), guest.height()));
    const int left = floor_of(from.x());
    const int top = floor_of(from.y());
    return QRect(left, top, ceil_of(to.x()) - left, ceil_of(to.y()) - top);
}

QRectF PixelMapping::picture(QSize guest) const {
    const QSize drawn = drawn_size(guest);
    return QRectF(origin_.x() / device_pixel_ratio_, origin_.y() / device_pixel_ratio_,
                  drawn.width() / device_pixel_ratio_, drawn.height() / device_pixel_ratio_);
}

QSize PixelMapping::drawn_size(QSize guest) const {
    if (one_to_one()) return guest;
    return QSize(nearest(guest.width() * fit_), nearest(guest.height() * fit_));
}

}  // namespace rwin::host
