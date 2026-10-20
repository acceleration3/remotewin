#pragma once

#include <cstdint>

namespace rwin::host {

// What brought this desktop's keyboard focus to a mirror.
//
//   Press    a press in it, which activated the window in the guest by
//            itself, as injected input
//   Mapping  the compositor, as it mapped the mirror: no choice of the
//            user's, and none the guest made - a RAIL client makes its
//            windows without activating them, which a Wayland client
//            cannot ask for
//   Other    Alt+Tab, the taskbar, a window closing over it: the user's
enum class FocusCause : std::uint8_t { Press, Mapping, Other };

}  // namespace rwin::host
