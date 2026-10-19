#pragma once

#include "host/cursor/cursor_shape.hpp"
#include "proto/system_cursor.hpp"

#include <optional>

namespace rwin::host {

// This desktop's own cursor of the same meaning as a Windows system cursor:
// a shape the compositor draws by name (wp_cursor_shape_v1), sharp at any
// scale and in the user's theme. Nothing for IDC_PIN and IDC_PERSON, which
// no desktop cursor means; those are shown as the guest drew them.
[[nodiscard]] std::optional<CursorShape> native_shape_for(SystemCursor cursor) noexcept;

}  // namespace rwin::host
