#pragma once

#include <atomic>
#include <thread>

namespace rwin::guest {

// The guest half's own window that takes the foreground when the host's
// focus leaves every mirror, and the thread every foreground change of the
// guest half is made from (RAIL's marker window, docs/rail-model.md §2.6).
//
// With the marker in front, no window the host shows is active: carets
// stop, menus close, as on a desktop whose user went elsewhere. It is never
// reported, never captured, and stays out of Alt+Tab; made as rdpinit makes
// its own - a visible WS_POPUP of no size, hidden from UI Automation - and
// a tool window besides, since this desktop has Explorer's Alt+Tab.
//
// Without UIAccess, SetForegroundWindow is refused to a process that did
// not receive the last input. rdpshell's way round it, copied: a hotkey of
// its own (no modifiers, a virtual key no keyboard has), sent with
// SendInput while any held Shift, Ctrl and Alt are let go, makes this
// thread the last to receive input, and the foreground is then its to give.
class MarkerWindow {
public:
    MarkerWindow();
    ~MarkerWindow();

    MarkerWindow(const MarkerWindow&) = delete;
    MarkerWindow& operator=(const MarkerWindow&) = delete;
    MarkerWindow(MarkerWindow&&) = delete;
    MarkerWindow& operator=(MarkerWindow&&) = delete;

    // Null when it could not be made.
    [[nodiscard]] void* handle() const noexcept { return marker_.load(); }

    // Makes ``window`` the foreground, from this object's thread; returns at
    // once. The newest request wins over one not yet carried out.
    void activate(void* window);

    // Called from the windows' procedure, on this object's thread.
    void carry_out();
    void hotkey_heard() noexcept { hotkey_heard_ = true; }
    void timed_out() noexcept { timed_out_ = true; }

private:
    void run();
    void register_hotkey();
    [[nodiscard]] bool take_with_hotkey(void* window);

    std::thread worker_;
    std::atomic<void*> marker_{nullptr};
    std::atomic<void*> keys_{nullptr};
    std::atomic<void*> wanted_{nullptr};
    std::atomic<unsigned long> thread_id_{0};
    std::atomic<bool> ready_{false};
    int hotkey_id_ = 0;
    unsigned int hotkey_key_ = 0;
    bool carrying_out_ = false;
    bool hotkey_heard_ = false;
    bool timed_out_ = false;
};

}  // namespace rwin::guest
