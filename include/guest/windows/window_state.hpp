#pragma once

#include <cstdint>

namespace rwin::guest {

class DesktopStandIn;

// Puts a guest window in the state its mirror has on the host: minimised,
// maximised, active or not.
//
// Through ShowWindow, so Windows does everything it does for a state
// change made any other way — WM_CANCELMODE to a window on its way to
// minimised, which is what ends a menu it had open. A mirror that lost
// the focus to the rest of the desktop is a window deactivated, and it
// is deactivated the only way Windows knows: the foreground goes to
// another window, the stand-in for that desktop, and Windows closes the
// menu, the dropped list and the capture as it does for any window whose
// activation went elsewhere.
class WindowState {
public:
    explicit WindowState(const DesktopStandIn& elsewhere) noexcept : elsewhere_(elsewhere) {}

    void apply(std::uint64_t window_handle, bool minimized, bool maximized,
               bool active) const;

private:
    const DesktopStandIn& elsewhere_;
};

}  // namespace rwin::guest
