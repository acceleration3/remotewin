#pragma once

#include "proto/min_max.hpp"
#include "proto/window_id.hpp"

namespace rwin::guest {
[[nodiscard]] MinMax read_min_max(void* window, WindowId id);

}
