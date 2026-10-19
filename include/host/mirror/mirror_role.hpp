#pragma once

#include "proto/window_id.hpp"

#include <cstdint>

namespace rwin::host {

class GuestWindow;
class GuestWindows;

// What a guest window's mirror is to this desktop (wire 6's roles):
//
//   a toplevel     a window of its own, which the compositor places; one
//                  owned and standing alone (a dialog, Win32Style::
//                  stands_alone) is kept above its owner's (set_parent)
//   a popup        placed against the window it opens from - a menu, a
//                  list, a tip, any owned popup - where it was meant to
//                  open (PopupLayout), and flipped or slid by the
//                  compositor against the output it is on
//   a subsurface   a part of the window it belongs to: kept with its
//                  neighbours in the z-order (Office's frame shadows), or
//                  owned, never activated and wholly inside its owner
//
// ``parent`` is the window the role is of: the owner a toplevel is kept
// above, or the window a popup or subsurface is placed against.
struct MirrorRole {
    enum class Kind : std::uint8_t { Toplevel, Popup, Subsurface };

    Kind kind = Kind::Toplevel;
    WindowId parent;

    [[nodiscard]] bool operator==(const MirrorRole&) const noexcept = default;

    // A parent the guest has not told of is no parent: the window stands
    // on its own.
    [[nodiscard]] static MirrorRole of(const GuestWindow& window, const GuestWindows& windows);
};

[[nodiscard]] const char* name_of(MirrorRole::Kind kind) noexcept;

}  // namespace rwin::host
