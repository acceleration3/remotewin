#pragma once

#include "guest/capture/frame_signal.hpp"
#include "guest/windows/desktop_settings.hpp"
#include "guest/windows/marker_window.hpp"
#include "proto/activate.hpp"
#include "proto/feature_set.hpp"
#include "proto/sys_command.hpp"
#include "proto/sys_menu.hpp"
#include "proto/window_id.hpp"
#include "proto/window_move.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

namespace rwin::guest {

class Outbox;
class WindowDirectory;
class WindowTracker;
class ZOrderReport;
class MoveSizeReport;

// The guest half's window thread: what Windows says of the session's
// windows, and what the host asks done to them, in arrival order on one
// thread, as rdpshell runs them on its one worker queue
// (docs/rail-model.md §2.2).
//
// Windows speaks through out-of-context WinEvent hooks over rdpshell's
// eight ranges, on this thread's queue; EnumWindows is used once, for the
// first full synchronisation. The thread is a single-threaded COM
// apartment, which the shell's undocumented interfaces it asks need.
class WindowWatch {
public:
    WindowWatch(WindowDirectory& directory, Outbox& outbox, FeatureSet features);
    ~WindowWatch();

    WindowWatch(const WindowWatch&) = delete;
    WindowWatch& operator=(const WindowWatch&) = delete;
    WindowWatch(WindowWatch&&) = delete;
    WindowWatch& operator=(WindowWatch&&) = delete;

    // From any thread; carried out on the window thread, in order.
    void ask(const Activate& request);
    void ask(const SysCommand& request);
    void ask(const SysMenu& request);
    void ask(const WindowMove& request);
    // The host's compositor dismissed the window's popup: its menu or list
    // is closed here too.
    void dismiss(WindowId popup);
    // ``window``'s frames now lie elsewhere in its frame: its GEOMETRY is
    // said again.
    void reread(WindowId window);

    // Called from the hook, on the window thread.
    void heard(unsigned long event, void* window, long child);

private:
    struct Event {
        unsigned long event = 0;
        void* window = nullptr;
        long child = 0;

        [[nodiscard]] bool operator==(const Event&) const noexcept = default;
    };
    struct Dismissal {
        WindowId popup;
    };
    using Request = std::variant<Activate, SysCommand, SysMenu, WindowMove, WindowId, Dismissal>;
    struct Parts;

    void run();
    void serve(Parts& parts);
    void pump();
    void take(Parts& parts, const Event& event);
    void settle(Parts& parts);
    void carry_out(Parts& parts, const Request& request);
    void activate(Parts& parts, const Activate& request);
    void say_work_area();
    void queue(Request request);

    WindowDirectory& directory_;
    Outbox& outbox_;
    const FeatureSet features_;
    FrameSignal wake_;
    DesktopSettings settings_{wake_};
    MarkerWindow marker_;
    std::vector<Event> heard_;
    std::mutex requests_lock_;
    std::vector<Request> requests_;
    std::atomic<bool> running_{true};
    std::thread worker_;
};

}  // namespace rwin::guest
