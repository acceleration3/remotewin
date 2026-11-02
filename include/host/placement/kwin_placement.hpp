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

class KWinPlacement {
public:
    KWinPlacement(EventLoop& loop, BusConnection& bus);
    ~KWinPlacement();

    KWinPlacement(const KWinPlacement&) = delete;
    KWinPlacement& operator=(const KWinPlacement&) = delete;
    KWinPlacement(KWinPlacement&&) = delete;
    KWinPlacement& operator=(KWinPlacement&&) = delete;

    [[nodiscard]] bool active() const noexcept { return watcher_ != nullptr && watcher_->running(); }

    struct Skips {
        bool taskbar = false;
        bool switcher = false;
    };

    [[nodiscard]] static std::string tag_for(std::uint64_t window);
    [[nodiscard]] static std::string caption_tag(std::uint64_t window, Skips skips);
    [[nodiscard]] static std::optional<std::uint64_t> window_of(std::string_view tag);

    struct Report {
        BoxF frame;
        bool dragged = false;
        bool by_state = false;
        bool minimized = false;
    };

    void place(std::uint64_t window, PointF corner);
    void send_to_output(std::uint64_t window, const std::string& output);

    [[nodiscard]] const OutputLayout& outputs() const noexcept { return outputs_; }
    void prefer_output(std::string name) { outputs_.prefer(std::move(name)); }

    Signal<std::uint64_t, PointF, std::int64_t> window_placed;
    Signal<std::uint64_t> window_not_placed;
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

}
