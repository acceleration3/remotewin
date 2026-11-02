#pragma once

#include "host/geometry/view_mapping.hpp"
#include "host/wayland/popup_placement.hpp"

namespace rwin::host {

class GuestWindow;

[[nodiscard]] PopupPlacement popup_layout(const GuestWindow& popup, const GuestWindow& parent,
                                          const ViewMapping& parent_view, Size popup_size);

[[nodiscard]] Point subsurface_layout(const GuestWindow& part, const GuestWindow& parent,
                                      const ViewMapping& parent_view);

}
