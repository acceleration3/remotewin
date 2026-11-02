#pragma once

#include "proto/key_stroke.hpp"
#include "proto/window_id.hpp"

#include <optional>

namespace rwin::host {

struct KeyPress;

[[nodiscard]] std::optional<KeyStroke> stroke_for(const KeyPress& press, WindowId window);

}
