// GuestWindow's opaque region: what of a picture the compositor may draw
// without blending. Windows' capture leaves a window region's outside clear
// (alpha 0, measured on WPF's rounded WindowChrome) and the inside opaque, so
// the region is opaque for a window that draws no alpha of its own; nothing
// is for any other.

#include "host/link/guest_window.hpp"

#include <cstdio>
#include <vector>

namespace {

using rwin::Rect;
using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::Box;
using rwin::host::GuestWindow;

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

// WPF's 8 px rounded corners on a 900x600 window, in GetRegionData's rows.
const std::vector<Rect> kRounded{Rect{5, 0, 890, 1}, Rect{2, 1, 896, 1}, Rect{1, 2, 898, 3}, Rect{0, 5, 900, 590},
                                 Rect{1, 595, 898, 3}, Rect{2, 598, 896, 1}, Rect{5, 599, 890, 1}};

WindowFields window(std::vector<Rect> region, Rect picture, std::uint32_t extended) {
    WindowFields fields;
    fields.geometry.frame = Rect{100, 80, 900, 600};
    fields.geometry.picture = picture;
    fields.geometry.region = std::move(region);
    fields.style.extended = extended;
    return fields;
}

void a_region_is_opaque() {
    const GuestWindow rounded{WindowId{0x1234, 1}, window(kRounded, Rect{0, 0, 900, 600}, 0)};
    const std::vector<Box> opaque = rounded.opaque_region();
    check(opaque.size() == kRounded.size(), "every row of the region is opaque");
    check(opaque.size() > 3 && opaque[3] == (Box{0, 5, 900, 590}), "where the region has it, in picture pixels");
}

void against_the_picture() {
    const GuestWindow trimmed{WindowId{0x1234, 1}, window({Rect{10, 10, 50, 40}}, Rect{4, 6, 880, 590}, 0)};
    const std::vector<Box> opaque = trimmed.opaque_region();
    check(opaque.size() == 1 && opaque[0] == (Box{6, 4, 50, 40}), "measured from the picture's corner");
}

void nothing_without_a_region() {
    const GuestWindow framed{WindowId{0x1234, 1}, window({}, Rect{0, 0, 900, 600}, 0)};
    check(framed.opaque_region().empty(), "a frame of DWM's is translucent at its corners and edge");
}

void nothing_of_a_layered_window() {
    const GuestWindow layered{WindowId{0x1234, 1},
                              window(kRounded, Rect{0, 0, 900, 600}, WindowFields::Style::kLayered)};
    check(layered.opaque_region().empty(), "a layered window's alpha is its own");
    check(!layered.region().empty(), "though its shape is still its region");
}

}  // namespace

int main() {
    a_region_is_opaque();
    against_the_picture();
    nothing_without_a_region();
    nothing_of_a_layered_window();
    if (failures == 0) std::printf("guest_window: ok\n");
    return failures == 0 ? 0 : 1;
}
