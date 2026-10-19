#pragma once

#include "host/geometry/view_mapping.hpp"
#include "host/wayland/popup_placement.hpp"

namespace rwin::host {

class GuestWindow;

// Where a popup's mirror opens against its parent's (R2, popups by intent):
// from the rectangle the guest says it was meant to open from - a menu bar
// item, the item of the menu a submenu opens out of, a combo box, the
// pointer - at the edge and in the direction it says, given way at the
// output's edges as it allows. A popup with no intent but where Windows put
// it opens there exactly: a rectangle of one unit at that point (clamped
// into the parent, as an anchor has to be, the rest an offset), sliding
// only to stay on the output. A tooltip keeps Windows' own offset from the
// pointer.
//
// In the parent's logical units, through its mapping: the guest says
// everything relative to frames, and a mirror's surface is the picture
// within the frame.
[[nodiscard]] PopupPlacement popup_layout(const GuestWindow& popup, const GuestWindow& parent,
                                          const ViewMapping& parent_view, Size popup_size);

// Where a subsurface's mirror goes on its parent's: the guest's own place
// for it, relative to the parent's picture, in the parent's logical units.
[[nodiscard]] Point subsurface_layout(const GuestWindow& part, const GuestWindow& parent,
                                      const ViewMapping& parent_view);

}  // namespace rwin::host
