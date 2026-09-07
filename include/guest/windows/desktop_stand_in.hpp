#pragma once

#include <atomic>
#include <thread>

namespace rwin::guest {

// The rest of the host desktop, as a window Windows can give the
// foreground to.
//
// When a mirror loses the focus on the host — the user clicked the
// taskbar, another application, the desktop — the guest window has been
// deactivated in every sense but Windows'. Windows only deactivates a
// window by activating another, and it then does everything that follows
// from that itself: WM_ACTIVATEAPP, WM_ACTIVATE, WM_KILLFOCUS, the mouse
// capture released, the menu and the dropped list closed. So this is the
// other window: one pixel, off the screen where Windows itself parks what
// is not to be seen, a tool window so no taskbar shows it.
//
// It lives on a thread of its own because a window needs its messages
// pumped, and the session's thread is busy.
class DesktopStandIn {
public:
    DesktopStandIn();
    ~DesktopStandIn();

    DesktopStandIn(const DesktopStandIn&) = delete;
    DesktopStandIn& operator=(const DesktopStandIn&) = delete;
    DesktopStandIn(DesktopStandIn&&) = delete;
    DesktopStandIn& operator=(DesktopStandIn&&) = delete;

    // Becomes the foreground window, deactivating whichever was.
    void take_foreground() const;

private:
    void run();

    std::thread worker_;
    std::atomic<void*> window_{nullptr};
    std::atomic<unsigned long> thread_id_{0};
};

}  // namespace rwin::guest
