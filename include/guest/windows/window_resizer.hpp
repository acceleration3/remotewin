#pragma once

#include <cstdint>

namespace rwin::guest {

// Gives a guest window the size the host asked for.
//
// Exactly that size, and where the window already is. The guest's screen
// is not a limit on a window nobody is looking at directly: the mirror is
// on another desktop, of another size, and a window may be larger than
// the display it is not being shown on. Windows honours a size given
// through SetWindowPos whatever the monitor is — the sizing loop's limits
// are the sizing loop's, not the window's — and DWM composes the whole
// window either way, so the capture is whole too.
//
// Answering with a size other than the one asked for is what a host
// cannot use: the mirror is already that size, the reply arrives after
// the user has dragged further, and the window jumps back under them.
class WindowResizer {
public:
    void resize(std::uint64_t window_handle, std::uint32_t width,
                std::uint32_t height) const;
};

}  // namespace rwin::guest
