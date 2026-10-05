#pragma once

#include "proto/system_cursor.hpp"

#include <Qt>

#include <optional>

namespace rwin::host {

// This desktop's own cursor of the same meaning as a Windows system cursor:
// a shape Qt asks the compositor to draw by name (wp_cursor_shape_v1 where
// it is offered, the cursor theme otherwise), sharp at any scale and in the
// user's theme. Nothing for IDC_PIN and IDC_PERSON, which no desktop
// cursor means; those are shown as the guest drew them.
[[nodiscard]] std::optional<Qt::CursorShape> native_shape_for(SystemCursor cursor) noexcept;

}  // namespace rwin::host
