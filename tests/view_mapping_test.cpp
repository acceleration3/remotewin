#include "host/geometry/view_mapping.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::host::Box;
using rwin::host::Point;
using rwin::host::PointF;
using rwin::host::Size;
using rwin::host::ViewMapping;

void a_fraction_of_a_unit_is_cropped_not_stretched() {
    const ViewMapping view = ViewMapping::one_to_one(Size{1001, 601}, 1.2);
    check(view.logical() == (Size{834, 500}), "1001x601 pixels at 1.2 show on 834x500 units");
    check(std::abs(view.source_width() - 1000.8) < 1e-9 && std::abs(view.source_height() - 600.0) < 1e-9,
          "and 1000.8x600 of the pixels: the fraction past the last unit is cropped");
    check(view.source_width() <= 1001 && view.source_height() <= 601, "never more than the picture has");
}

void a_size_asked_for_comes_back_as_itself() {
    for (int scale120 = 120; scale120 <= 300; ++scale120) {
        const double scale = scale120 / 120.0;
        for (int units = 1; units <= 1200; ++units) {
            const Size picture = ViewMapping::picture_for(Size{units, units}, scale);
            if (ViewMapping::one_to_one(picture, scale).logical() != Size{units, units}) {
                std::printf("  %d units at %.4f asked %d pixels\n", units, scale, picture.width);
                check(false, "the picture asked for a size shows on that size");
                return;
            }
        }
    }
}

void a_pixel_is_found_where_it_is_shown() {
    const ViewMapping view = ViewMapping::one_to_one(Size{1000, 800}, 1.25);
    bool all = true;
    for (int x = 0; x < 1000; ++x) {
        if (view.picture_point(view.logical_point(Point{x, x % 800})) != Point{x, x % 800}) all = false;
    }
    check(all, "every pixel's corner is that pixel again");
    check(view.picture_point(PointF{0.79, 0.81}) == Point{0, 1}, "a point between pixels is the pixel it is on");
}

void rectangles_round_outward_and_inward() {
    const ViewMapping view = ViewMapping::one_to_one(Size{1200, 1200}, 1.2);
    check(view.logical_box(Box{7, 7, 10, 10}) == (Box{5, 5, 10, 10}), "covering rounds outward: 5.83 to 14.17 is 5..15");
    check(view.logical_box_inside(Box{7, 7, 10, 10}) == (Box{6, 6, 8, 8}), "inside rounds inward: 6..14");
    check(view.logical_box_inside(Box{7, 7, 1, 1}).empty(), "a pixel holds no whole unit at 1.2");
}

void a_stretched_picture_maps_by_its_own_ratio() {
    const ViewMapping view = ViewMapping::stretched(Size{2000, 1000}, Size{1000, 250});
    check(view.logical() == (Size{1000, 250}), "shown at the size given");
    check(view.picture_point(PointF{10.5, 2.0}) == Point{21, 8}, "two pixels a unit across, four down");
    check(std::abs(view.source_width() - 2000.0) < 1e-9 && std::abs(view.source_height() - 1000.0) < 1e-9,
          "the whole picture");
}

void a_side_under_one_unit_shows_its_own_pixels() {
    const ViewMapping view = ViewMapping::one_to_one(Size{1, 40}, 1.2);
    check(view.logical() == (Size{1, 33}), "a 1x40 picture at 1.2 shows on 1x33 units");
    check(view.source_width() <= 1.0 && std::abs(view.source_width() - 1.0) < 1e-9,
          "its one pixel across is the source, not 1.2 of a 1-pixel buffer");
    check(std::abs(view.source_height() - 39.6) < 1e-9, "down it is one to one as before");
}

}

int main() {
    a_fraction_of_a_unit_is_cropped_not_stretched();
    a_size_asked_for_comes_back_as_itself();
    a_pixel_is_found_where_it_is_shown();
    rectangles_round_outward_and_inward();
    a_stretched_picture_maps_by_its_own_ratio();
    a_side_under_one_unit_shows_its_own_pixels();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("view mapping: all checks passed\n");
    return EXIT_SUCCESS;
}
