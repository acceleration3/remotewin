// PictureLimits: the window's WM_GETMINMAXINFO applied side by side to the
// mirror's picture, as RAIL's client applies Min Max Info - a side the
// window leaves unlimited does not take the other side's limit with it, and
// no largest is ever under the smallest.

#include "host/mirror/picture_limits.hpp"

#include <cstdio>

namespace {

using rwin::MinMax;
using rwin::host::GuestWindow;
using rwin::host::PictureLimits;
using rwin::host::Size;

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

// Seven pixels of invisible border on the left, right and bottom, none at
// the top, as Windows 11 has them; the picture is the frame.
constexpr GuestWindow::Margins kMargins{7, 0, 7, 7};

MinMax tracks(int min_width, int min_height, int max_width, int max_height) {
    MinMax limits;
    limits.min_track_width = min_width;
    limits.min_track_height = min_height;
    limits.max_track_width = max_width;
    limits.max_track_height = max_height;
    return limits;
}

void track_sizes_lose_the_borders() {
    const PictureLimits picture = PictureLimits::of(tracks(314, 207, 1934, 1107), kMargins, Size{}, Size{});
    check(picture.smallest == Size{300, 200}, "the smallest is the minimum track less the borders");
    check(picture.largest == Size{1920, 1100}, "the largest is the maximum track less the borders");
}

// The defect: a minimum height that comes to nothing dropped the minimum
// width with it.
void a_side_without_a_minimum_keeps_the_other() {
    const PictureLimits picture = PictureLimits::of(tracks(414, 0, 0, 0), kMargins, Size{}, Size{});
    check(picture.smallest == Size{400, 0}, "the minimum width stays when the height has none");
    check(picture.largest == Size{}, "no maximum where neither track nor work area says one");
}

void a_side_without_a_maximum_keeps_the_other() {
    const PictureLimits picture = PictureLimits::of(tracks(0, 0, 814, 0), kMargins, Size{}, Size{});
    check(picture.largest == Size{800, 0}, "the maximum width stays when the height has none");
}

void the_work_area_bounds_the_largest() {
    const PictureLimits picture = PictureLimits::of(tracks(0, 0, 4000, 900), kMargins, Size{0, 10}, Size{2560, 1392});
    check(picture.largest == Size{2560, 883}, "each side is the tighter of the track and the work area");
    const PictureLimits none = PictureLimits::of(std::nullopt, kMargins, Size{0, 10}, Size{2560, 1392});
    check(none.smallest == Size{} && none.largest == Size{2560, 1382}, "with no MinMax yet, the work area alone");
}

// xdg_toplevel refuses a maximum under the minimum (invalid_size), and
// Windows keeps a window at its minimum tracking size anyway.
void the_largest_is_never_under_the_smallest() {
    const PictureLimits picture = PictureLimits::of(tracks(3014, 207, 0, 0), kMargins, Size{}, Size{2560, 1392});
    check(picture.largest.width == 3000, "a minimum wider than the work area raises the maximum to it");
    check(picture.largest.height == 1392, "the other side keeps its maximum");
}

}  // namespace

int main() {
    track_sizes_lose_the_borders();
    a_side_without_a_minimum_keeps_the_other();
    a_side_without_a_maximum_keeps_the_other();
    the_work_area_bounds_the_largest();
    the_largest_is_never_under_the_smallest();
    if (failures == 0) std::printf("picture_limits: all passed\n");
    return failures == 0 ? 0 : 1;
}
