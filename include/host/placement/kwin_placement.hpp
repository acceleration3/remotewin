#pragma once

#include "host/geometry/box_f.hpp"
#include "host/geometry/point_f.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "host/placement/kwin_script.hpp"
#include "host/placement/screen_mapping.hpp"

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

    [[nodiscard]] static std::string tag_for(std::uint64_t window);
    [[nodiscard]] static std::optional<std::uint64_t> window_of(std::string_view tag);

    // ``frame`` in the desktop's logical units. The size is set with the
    // place - KWin has no move alone - and is the size the window is to
    // be: as KWin maps a window it may not have its final size yet, and
    // a place set with that size is a resize the client then honours.
    void place(std::uint64_t window, const BoxF& frame);
    // Sends a window to the output of that name, keeping its state - a
    // maximised window is maximised there - now or as KWin maps it; said
    // done as a placement is (window_placed, where its frame then is).
    void send_to_output(std::uint64_t window, const std::string& output);

    // The first output, as KWin lays it out - its primary: its corner and
    // its scale. Nothing before KWin has said.
    [[nodiscard]] std::optional<ScreenMapping> primary_output() const;
    [[nodiscard]] std::string primary_output_name() const;

    // The corner the window's frame is at now, and how long it took.
    Signal<std::uint64_t, PointF, std::int64_t> window_placed;
    // KWin did not find the window in time.
    Signal<std::uint64_t> window_not_placed;
    Signal<std::uint64_t, const BoxF&> window_moved;

private:
    struct Output {
        std::string name;
        PointF origin;
        double scale = 1.0;
    };
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
    std::vector<Output> outputs_;
    std::unique_ptr<EventLoop::Timer> expiry_;
    std::unique_ptr<EventLoop::Timer> rewatch_;
    int watch_tries_ = 0;
    std::uint32_t scripts_ = 0;
};

}  // namespace rwin::host
