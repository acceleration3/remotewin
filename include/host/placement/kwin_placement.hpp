#pragma once

#include "host/geometry/box_f.hpp"
#include "host/geometry/point_f.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "host/placement/kwin_script.hpp"
#include "host/placement/output_layout.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sd_bus_error;
struct sd_bus_message;
struct sd_bus_slot;

namespace rwin::host {

class BusConnection;

// Puts this process's toplevel windows where they are asked to be, and says
// where they are, on KWin - which, as any Wayland compositor, gives a
// client no way to do either itself.
//
// Through KWin's scripting: a script that stays loaded watches the
// process's windows and reports, over D-Bus to this object, every place one
// of them is put by anything but an interactive drag still under way; and
// a script per placement finds its window - now, or as KWin maps it, so a
// new window is never seen anywhere else - sets its frame, and says so. A
// script cannot be sent anything once it runs, so a placement is a script
// of its own; loading one is a D-Bus round trip, which is not waited for.
//
// A window is known to the scripts by a suffix of its title (tag_for):
// characters that are not drawn, encoding the guest window's handle,
// which KWin keeps.
class KWinPlacement {
public:
    KWinPlacement(EventLoop& loop, BusConnection& bus);
    ~KWinPlacement();

    KWinPlacement(const KWinPlacement&) = delete;
    KWinPlacement& operator=(const KWinPlacement&) = delete;
    KWinPlacement(KWinPlacement&&) = delete;
    KWinPlacement& operator=(KWinPlacement&&) = delete;

    // On KWin, with its scripting reachable, this object on the bus and the
    // watching script running.
    [[nodiscard]] bool active() const noexcept { return watcher_ != nullptr && watcher_->running(); }

    // Which of KWin's lists a window is left out of (skipTaskbar,
    // skipSwitcher).
    struct Skips {
        bool taskbar = false;
        bool switcher = false;
    };

    [[nodiscard]] static std::string tag_for(std::uint64_t window);
    // tag_for, then one character more that says ``skips``: the watching
    // script reads it whenever the caption changes and leaves the window out
    // of the taskbar or the switcher, the one thing about a window's place
    // on a desktop xdg-shell has no word for.
    [[nodiscard]] static std::string caption_tag(std::uint64_t window, Skips skips);
    [[nodiscard]] static std::optional<std::uint64_t> window_of(std::string_view tag);

    // How KWin has a window, as its watching script reports it.
    struct Report {
        // Its frame, in the desktop's logical units.
        BoxF frame;
        // A move or resize its user made ended.
        bool dragged = false;
        // Maximised or fullscreen: the frame is the state's, not a place.
        bool by_state = false;
        bool minimized = false;
    };

    // The frame's corner, in the desktop's logical units. A move alone: the
    // size is the client's, and one set here from what this side last knew
    // would be a resize the client then honours - back to a size the window
    // had a moment ago.
    void place(std::uint64_t window, PointF corner);
    // Sends a window to the output of that name, keeping its state - a
    // maximised window is maximised there - now or as KWin maps it; said
    // done as a placement is (window_placed, where its frame then is).
    void send_to_output(std::uint64_t window, const std::string& output);

    // The outputs as KWin lays them out, and the one that stands for the
    // guest's monitor (``prefer_output``).
    [[nodiscard]] const OutputLayout& outputs() const noexcept { return outputs_; }
    void prefer_output(std::string name) { outputs_.prefer(std::move(name)); }

    // The corner the window's frame is at now, and how long it took.
    Signal<std::uint64_t, PointF, std::int64_t> window_placed;
    // KWin did not find the window in time.
    Signal<std::uint64_t> window_not_placed;
    // Its frame, state or minimised flag changed, or a drag of it ended.
    Signal<std::uint64_t, const Report&> window_moved;

private:
    struct Pending {
        std::uint64_t window = 0;
        std::int64_t since = 0;
        std::unique_ptr<KWinScript> script;
        bool failed = false;
    };

    static int placed_call(sd_bus_message* call, void* self, sd_bus_error* error);
    static int moved_call(sd_bus_message* call, void* self, sd_bus_error* error);
    static int outputs_call(sd_bus_message* call, void* self, sd_bus_error* error);
    void placed(std::string_view report);
    void moved(std::string_view report);
    void outputs(std::string_view report);
    [[nodiscard]] std::string script_name(std::string_view role);
    void run_for(std::uint64_t window, const std::string& source);
    void watch();
    void expire();

    BusConnection& bus_;
    std::string service_;
    sd_bus_slot* object_ = nullptr;
    std::unique_ptr<KWinScript> watcher_;
    std::vector<Pending> pending_;
    OutputLayout outputs_;
    std::unique_ptr<EventLoop::Timer> expiry_;
    std::unique_ptr<EventLoop::Timer> rewatch_;
    int watch_tries_ = 0;
    std::uint32_t scripts_ = 0;
};

}  // namespace rwin::host
