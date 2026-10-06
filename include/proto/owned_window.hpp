#pragma once

#include <cstdint>

namespace rwin {

// Whether a window with an owner is mirrored as a window of its own,
// attached to its owner's mirror, rather than as a popup placed against it.
//
// Both ends ask it, and have to agree: the host decides what kind of
// surface the mirror is, and the guest decides from the same answer
// whether focusing it may move the guest's foreground to it.
//
// A window that activates and is a window rather than a popup is one the
// user moves, types into and switches to: a dialog, a message box, a
// palette. Windows draws a caption on every overlapped window, so one
// without WS_POPUP had a caption until its program took it away to draw
// its own - WPF's WindowStyle=None, which is how Affinity's floating tool
// panels are made. Wayland gives a popup no keyboard focus and no move, so
// such a window is a toplevel attached to its owner's
// (xdg_toplevel.set_parent), which the compositor keeps above its owner,
// groups with it and leaves off the taskbar. A WS_POPUP without a caption
// - a menu, a dropdown, a tooltip, a borderless popup - stays a popup,
// placed exactly where its owner put it, and so does anything that never
// activates.
[[nodiscard]] constexpr bool owned_window_stands_alone(std::uint32_t style,
                                                       std::uint32_t extended_style) noexcept {
    constexpr std::uint32_t kWsCaption = 0x00C00000;
    constexpr std::uint32_t kWsPopup = 0x80000000;
    constexpr std::uint32_t kWsExNoActivate = 0x08000000;
    if ((extended_style & kWsExNoActivate) != 0) return false;
    return (style & kWsCaption) == kWsCaption || (style & kWsPopup) == 0;
}

}  // namespace rwin
