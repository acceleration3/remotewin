#pragma once

#include "host/geometry/box.hpp"

namespace rwin::host {

// The guest's primary monitor as the guest last said it, in its pixels:
// the whole of it, and the work area - what is left once the taskbar and
// the like have their room.
struct GuestDesktop {
    Box monitor;
    Box work_area;
};

}  // namespace rwin::host
