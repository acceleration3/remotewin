#pragma once

#include "proto/probe/window_truth.hpp"

struct HWND__;

namespace rwin::probe {
[[nodiscard]] WindowTruth read_window(HWND__* window);

}
