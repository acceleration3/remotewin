#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace rwin::guest {

// Notices that Windows decided a press was a window drag.
//
// Which pixels of a window can be dragged is the application's business,
// not something to be worked out from the outside: a window draws its own
// title bar wherever it likes, and asking it which part of itself a point
// is over answers for the frame it has, not for the gesture the user is
// making. So nothing is predicted. The press is delivered, Windows starts
// its own move loop, and that is the signal.
//
// The loop is then cancelled immediately, before the guest window has
// moved a pixel, and the host is told to take the drag over with its own
// compositor — where the window being dragged actually is.
//
// Only for this mirror's own windows (mirror_only): the hook sees every
// window on the guest's desktop, and another application's drag is its
// own mirror's to take, or Windows'. And only for a window nothing owns: an
// owned window - a floating tool panel, a dialog - keeps Windows' move
// loop, because applications that dock watch their own windows move
// (WM_MOVING, where the pointer is, where it ends) to show their dock
// targets and drop into them; the compositor moving the mirror instead
// leaves them nothing to watch. The mirror follows the guest window there.
class WindowDragWatcher {
public:
    WindowDragWatcher();
    ~WindowDragWatcher();

    WindowDragWatcher(const WindowDragWatcher&) = delete;
    WindowDragWatcher& operator=(const WindowDragWatcher&) = delete;
    WindowDragWatcher(WindowDragWatcher&&) = delete;
    WindowDragWatcher& operator=(WindowDragWatcher&&) = delete;

    // Windows whose drag began since the last call.
    [[nodiscard]] std::vector<std::uint64_t> drained();

    // The windows this mirror shows, as the window list last had them:
    // the only ones whose drags it takes.
    void mirror_only(std::vector<std::uint64_t> windows);
    [[nodiscard]] bool mirrors(std::uint64_t window_handle);

    // Called from the hook, which runs on this watcher's own thread.
    void note(std::uint64_t window_handle);

private:
    void listen();

    std::thread worker_;
    std::mutex mutex_;
    std::vector<std::uint64_t> started_;
    std::vector<std::uint64_t> mirrored_;
    std::atomic<unsigned long> thread_id_{0};
};

}  // namespace rwin::guest
