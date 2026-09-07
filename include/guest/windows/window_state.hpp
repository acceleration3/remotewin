#pragma once

#include <cstdint>

namespace rwin::guest {

// Puts a guest window in the state its mirror was put in on the host:
// minimised, maximised, or neither.
//
// Through ShowWindow, so Windows does everything it does for a state
// change made any other way — WM_CANCELMODE to a window on its way to
// minimised, which is what ends a menu it had open.
class WindowState {
public:
    void apply(std::uint64_t window_handle, bool minimized, bool maximized) const;
};

}  // namespace rwin::guest
