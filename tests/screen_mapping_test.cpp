#include "host/placement/screen_mapping.hpp"

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

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

using rwin::host::Point;
using rwin::host::PointF;
using rwin::host::ScreenMapping;

void a_guest_pixel_is_a_device_pixel_of_the_output() {
    const ScreenMapping mapping{PointF{0, 0}, 1.2};
    const PointF host = mapping.host_point(Point{152, 300});
    check(near(host.x, 152 / 1.2) && near(host.y, 250), "152,300 at 120% is 126.67,250");
    check(mapping.guest_point(host) == Point{152, 300}, "and back");
}

void every_guest_pixel_comes_back_to_itself() {
    const ScreenMapping mapping{PointF{0, 0}, 1.2};
    bool all = true;
    for (int x = -40; x <= 2600; ++x) {
        if (mapping.guest_point(mapping.host_point(Point{x, x})) != Point{x, x}) all = false;
    }
    check(all, "no guest pixel is lost to the round trip at a fractional scale");
}

void the_output_may_be_anywhere_in_the_layout() {
    const ScreenMapping mapping{PointF{2134, 120}, 1.0};
    check(mapping.host_point(Point{10, 20}) == PointF{2144, 140}, "an output right of another");
    check(mapping.guest_point(PointF{2134, 120}) == Point{0, 0}, "its corner is the guest's");
}

void a_rounded_report_is_the_nearest_pixel() {
    const ScreenMapping mapping{PointF{0, 0}, 1.2};
    // The compositor answering 127 for an asked 126.67 is still guest 152.
    check(mapping.guest_point(PointF{127, 0}) == Point{152, 0}, "127 logical is 152.4, guest 152");
}

}  // namespace

int main() {
    a_guest_pixel_is_a_device_pixel_of_the_output();
    every_guest_pixel_comes_back_to_itself();
    the_output_may_be_anywhere_in_the_layout();
    a_rounded_report_is_the_nearest_pixel();
    if (failures != 0) return EXIT_FAILURE;
    std::printf("screen_mapping: all passed\n");
    return EXIT_SUCCESS;
}
