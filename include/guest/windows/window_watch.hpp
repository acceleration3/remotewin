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

class WindowWatch {
public:
    WindowWatch(WindowDirectory& directory, Outbox& outbox, FeatureSet features);
    ~WindowWatch();

    WindowWatch(const WindowWatch&) = delete;
    WindowWatch& operator=(const WindowWatch&) = delete;
    WindowWatch(WindowWatch&&) = delete;
    WindowWatch& operator=(WindowWatch&&) = delete;

    void ask(const Activate& request);
    void ask(const SysCommand& request);
    void ask(const SysMenu& request);
    void ask(const WindowMove& request);
    void dismiss(WindowId popup);
    void reread(WindowId window);

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

}
