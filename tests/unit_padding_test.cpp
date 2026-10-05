#include "host/cursor/unit_padding.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using rwin::host::unit_padding;
using rwin::host::UnitPadding;

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

void a_cursor_at_six_fifths() {
    const auto padding = unit_padding(QSize(32, 32), QPoint(4, 4), 1.2);
    check(padding.has_value() && *padding == UnitPadding{QPoint(2, 2), QSize(36, 36), QSize(30, 30), QPoint(5, 5)},
          "32x32 with its hotspot at 4,4 at 1.2: 2 px left and above, 36 px, 30 units, hotspot at unit 5");
}

void whole_ratios_pad_to_the_ratio() {
    const auto two = unit_padding(QSize(32, 32), QPoint(3, 3), 2.0);
    check(two.has_value() && *two == UnitPadding{QPoint(1, 1), QSize(34, 34), QSize(17, 17), QPoint(2, 2)},
          "at 2 a hotspot on an odd pixel moves by one");
    const auto one = unit_padding(QSize(32, 30), QPoint(7, 9), 1.0);
    check(one.has_value() && *one == UnitPadding{QPoint(0, 0), QSize(32, 30), QSize(32, 30), QPoint(7, 9)},
          "at 1 nothing is padded");
}

void a_ratio_that_is_no_small_fraction_is_refused() {
    check(!unit_padding(QSize(32, 32), QPoint(0, 0), 1.2345678).has_value(), "1.2345678 has no p/q to pad to");
}

// Every scale a compositor offers (120ths, 1 to 2.5) and every hotspot: the
// padded picture is exactly its logical size at the ratio, and so is the
// hotspot.
void every_scale_lands_on_whole_units() {
    for (int twentieths = 20; twentieths <= 50; ++twentieths) {
        const double ratio = twentieths / 20.0;
        for (int hotspot = 0; hotspot < 32; ++hotspot) {
            const auto padding = unit_padding(QSize(32, 24), QPoint(hotspot, 31 - hotspot), ratio);
            if (!padding.has_value()) continue;
            const bool exact = std::abs(padding->logical.width() * ratio - padding->padded.width()) < 1e-6
                               && std::abs(padding->logical.height() * ratio - padding->padded.height()) < 1e-6
                               && std::abs(padding->logical_hotspot.x() * ratio - (hotspot + padding->offset.x())) < 1e-6
                               && std::abs(padding->logical_hotspot.y() * ratio - (31 - hotspot + padding->offset.y())) < 1e-6
                               && padding->padded.width() >= padding->offset.x() + 32;
            if (!exact) {
                std::printf("  ratio %.3f hotspot %d\n", ratio, hotspot);
                check(false, "a padded cursor is whole logical units, hotspot included");
                return;
            }
        }
    }
}

}  // namespace

int main() {
    a_cursor_at_six_fifths();
    whole_ratios_pad_to_the_ratio();
    a_ratio_that_is_no_small_fraction_is_refused();
    every_scale_lands_on_whole_units();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("unit padding: all checks passed\n");
    return EXIT_SUCCESS;
}
