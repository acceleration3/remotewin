#pragma once

#include "proto/min_max.hpp"
#include "proto/window_id.hpp"

namespace rwin::guest {

// What a window answers WM_GETMINMAXINFO with, asked as RAIL's server asks
// it: through SendMessageTimeout, one second and abort if hung, the
// structure prefilled with the system's own track limits so a window that
// does not answer still says something true.
[[nodiscard]] MinMax read_min_max(void* window, WindowId id);

}  // namespace rwin::guest
