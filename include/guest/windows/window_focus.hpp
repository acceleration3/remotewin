#pragma once

#include <cstdint>

namespace rwin::guest {

// Makes a guest window the one that injected input reaches.
//
// Input is delivered at the desktop, so it lands wherever the guest's
// foreground is. A mirror the host is typing into has to be that window.
//
// Refuses in the cases where taking the foreground destroys what the user
// was doing: while a menu or a dropdown is tracking, or for a window that
// belongs to another. Those all lose their grab when the foreground
// moves, and the application reads that as the user clicking away.
class WindowFocus {
public:
    void give_to(std::uint64_t window_handle) const;

private:
    // True while some transient thing owns the pointer and would be
    // dismissed. Menu flags do not cover a dropdown or a drag; an
    // outstanding mouse capture does.
    [[nodiscard]] static bool something_is_tracking();
};

}  // namespace rwin::guest
