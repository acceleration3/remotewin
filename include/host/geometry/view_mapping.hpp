#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"

namespace rwin::host {

// Where a guest picture's pixels land on a surface, and which pixel a point
// of the surface is.
//
// A guest pixel is a device pixel: the guest is given the output's size and
// its DPI is the output's scale, so it lays itself out as large as a native
// window would. One to one, a picture of N pixels is shown on N / scale
// logical units rounded down, and the fraction of a unit past the last whole
// one is cropped from the right and bottom edge (a viewport's source) rather
// than the picture stretched by it: at 120% a 1001-pixel window is 834 units
// showing 1000.8 of its pixels, at their own size. Asked for a surface of L
// units, the guest is asked for L * scale pixels rounded up, which shows on
// L units again: a resize asked for never comes back as another one.
//
// Stretched, the picture is shown over a size it is given, whatever it is.
class ViewMapping {
public:
    ViewMapping() = default;

    [[nodiscard]] static ViewMapping one_to_one(Size picture, double scale) noexcept;
    [[nodiscard]] static ViewMapping stretched(Size picture, Size logical) noexcept;

    // The picture a surface of ``logical`` units shows whole, one to one.
    [[nodiscard]] static Size picture_for(Size logical, double scale) noexcept;

    // The surface's size, in its logical units.
    [[nodiscard]] Size logical() const noexcept { return logical_; }
    // How much of the picture is shown, from its top left, in its pixels.
    [[nodiscard]] double source_width() const noexcept { return logical_.width * x_; }
    [[nodiscard]] double source_height() const noexcept { return logical_.height * y_; }

    // The pixel under a point of the surface.
    [[nodiscard]] Point picture_point(PointF logical) const noexcept;
    // Where a pixel's corner is on the surface.
    [[nodiscard]] PointF logical_point(Point picture) const noexcept;
    // The whole units a picture rectangle covers any part of: a shape cut to
    // it never cuts into the picture.
    [[nodiscard]] Box logical_box(const Box& picture) const noexcept;
    // The whole units entirely inside a picture rectangle: a pointer kept to
    // it is never over a pixel outside (ClipCursor's, as a confinement).
    // Empty when it holds no whole unit.
    [[nodiscard]] Box logical_box_inside(const Box& picture) const noexcept;

private:
    ViewMapping(Size logical, double x, double y) noexcept : logical_(logical), x_(x), y_(y) {}

    Size logical_;
    // Picture pixels per logical unit, across and down.
    double x_ = 1.0;
    double y_ = 1.0;
};

}  // namespace rwin::host
