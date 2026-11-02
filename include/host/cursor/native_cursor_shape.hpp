#pragma once

#include "host/cursor/cursor_shape.hpp"
#include "proto/system_cursor.hpp"

#include <optional>

namespace rwin::host {
[[nodiscard]] std::optional<CursorShape> native_shape_for(SystemCursor cursor) noexcept;

}
