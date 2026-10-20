#pragma once

#include "host/geometry/size.hpp"
#include "host/link/guest_window.hpp"
#include "proto/min_max.hpp"

#include <optional>

namespace rwin::host {

// The sizes a toplevel mirror's picture may be given, in guest pixels, from
// what the window answered WM_GETMINMAXINFO (MinMax: RAIL's Server Min Max
// Info, which a client applies to its window, MS-RDPERP 3.2.5.2.7.1). Each
// side on its own; a side of 0 is no limit on that side.
struct PictureLimits {
    Size smallest;
    Size largest;

    [[nodiscard]] bool operator==(const PictureLimits&) const noexcept = default;

    // MinMax's track sizes are the window's whole rectangle, its invisible
    // borders (``margins``) included, and the picture is the frame less
    // ``padding``. The largest is held to ``work_area`` as well (empty: not
    // known), so a drag stops where the guest window would. A largest
    // smaller than the smallest is the smallest: Windows keeps a window at
    // its minimum tracking size whatever its maximum says, and xdg-shell
    // refuses a maximum under the minimum.
    [[nodiscard]] static PictureLimits of(const std::optional<MinMax>& limits, const GuestWindow::Margins& margins,
                                          Size padding, Size work_area) noexcept;
};

}  // namespace rwin::host
