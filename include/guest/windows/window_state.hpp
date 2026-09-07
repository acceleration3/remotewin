#pragma once

#include <cstdint>

namespace rwin::guest {

// Puts a guest window in the state its mirror has on the host: minimised,
// maximised, active or not.
//
// Through ShowWindow, so Windows does everything it does for a state
// change made any other way — WM_CANCELMODE to a window on its way to
// minimised, which is what ends a menu it had open. A mirror that lost
// the focus to the rest of the desktop is a window deactivated, and a
// deactivated window's modal state — its menu, its drag — is cancelled
// the same way, because the guest has no other window for the foreground
// to go to.
class WindowState {
public:
    void apply(std::uint64_t window_handle, bool minimized, bool maximized,
               bool active) const;
};

}  // namespace rwin::guest
