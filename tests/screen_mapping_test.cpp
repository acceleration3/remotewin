#include "host/placement/output_layout.hpp"
#include "host/placement/screen_mapping.hpp"

#include <cmath>
#include <optional>
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
using rwin::host::BoxF;
using rwin::host::OutputLayout;
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
    check(mapping.guest_point(PointF{127, 0}) == Point{152, 0}, "127 logical is 152.4, guest 152");
}

OutputLayout two_outputs(const char* guest_output) {
    OutputLayout layout;
    layout.replace({OutputLayout::Output{"HDMI-A-3", PointF{2134, 120}, PointF{1920, 1080}, 1.0},
                    OutputLayout::Output{"DP-2", PointF{0, 0}, PointF{2133.33, 1200}, 1.2}});
    layout.prefer(guest_output);
    return layout;
}

void the_named_output_is_the_guests_and_the_first_is_the_fallback() {
    check(two_outputs("DP-2").guest_output_name() == "DP-2", "named");
    check(two_outputs("HDMI-A-3").guest_output_name() == "HDMI-A-3", "named, first");
    check(two_outputs("DP-9").guest_output_name() == "HDMI-A-3", "an unknown name falls back to the first");
}

void a_window_on_either_output_is_on_the_guest_monitor() {
    const OutputLayout layout = two_outputs("DP-2");
    check(layout.guest_point(BoxF{299, 300, 1254, 500}) == Point{359, 360}, "DP-2 at its own scale");
    check(layout.guest_point(BoxF{2234, 220, 800, 500}) == Point{100, 100}, "HDMI-A-3 at scale 1");
}

void a_guest_move_is_placed_on_the_output_the_window_is_on() {
    const OutputLayout layout = two_outputs("DP-2");
    const BoxF on_hdmi{2300, 300, 800, 500};
    const BoxF on_dp{300, 300, 800, 500};
    check(layout.host_point(Point{100, 100}, on_hdmi) == PointF{2234, 220}, "stays on HDMI-A-3");
    check(layout.host_point(Point{120, 120}, on_dp) == PointF{100, 100}, "stays on DP-2");
    check(layout.host_point(Point{120, 120}, std::nullopt) == PointF{100, 100}, "a new window is on the guest's output");
    const Point guest = layout.guest_point(on_hdmi);
    const PointF back = layout.host_point(guest, on_hdmi);
    check(layout.guest_point(BoxF{back.x, back.y, 800, 500}) == guest, "no drift");
}

void the_guest_window_is_kept_whole_on_the_guest_monitor() {
    const OutputLayout layout = two_outputs("DP-2");
    check(layout.guest_point(BoxF{-300, -50, 800, 500}) == Point{0, 0}, "not left of or above it");
    check(layout.guest_point(BoxF{1700, 100, 800, 500}).x == 2560 - 960, "not past its right edge");
    check(layout.guest_point(BoxF{299, 1100, 1254, 800}).y == 1440 - static_cast<int>(800 * 1.2 + 0.5), "not below it");
}

}

int main() {
    the_named_output_is_the_guests_and_the_first_is_the_fallback();
    a_window_on_either_output_is_on_the_guest_monitor();
    a_guest_move_is_placed_on_the_output_the_window_is_on();
    the_guest_window_is_kept_whole_on_the_guest_monitor();
    a_guest_pixel_is_a_device_pixel_of_the_output();
    every_guest_pixel_comes_back_to_itself();
    the_output_may_be_anywhere_in_the_layout();
    a_rounded_report_is_the_nearest_pixel();
    if (failures != 0) return EXIT_FAILURE;
    std::printf("screen_mapping: all passed\n");
    return EXIT_SUCCESS;
}
