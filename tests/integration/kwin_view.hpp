#pragma once

#include "host/loop/event_loop.hpp"

#include <sys/types.h>

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
class KWinScript;
}

namespace rwin::integration {
class KWinView {
public:
    struct Seen {
        std::int64_t at_ns = 0;
        std::string what;
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
        int maximize_mode = 0;
        bool minimized = false;
        bool resizing = false;
    };

    KWinView(host::EventLoop& loop, host::BusConnection& bus, pid_t host);
    ~KWinView();

    KWinView(const KWinView&) = delete;
    KWinView& operator=(const KWinView&) = delete;
    KWinView(KWinView&&) = delete;
    KWinView& operator=(KWinView&&) = delete;

    struct Stacked {
        std::string title;
        bool popup = false;
        bool transient = false;
        bool active = false;
        bool skip_taskbar = false;
        bool skip_switcher = false;
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
    };

    [[nodiscard]] bool watching() const noexcept { return watching_; }
    [[nodiscard]] const std::optional<Seen>& now() const noexcept { return now_; }
    [[nodiscard]] const std::vector<Seen>& journal() const noexcept { return journal_; }
    [[nodiscard]] std::size_t done() const noexcept { return done_; }

    void survey();
    [[nodiscard]] const std::vector<Stacked>& stacking() const noexcept { return stacking_; }
    [[nodiscard]] std::size_t surveys() const noexcept { return surveys_; }
    void activate(const std::string& title);
    void close(const std::string& title);

    void set_frame(double x, double y, double width, double height);
    void maximize(bool maximized);
    void minimize(bool minimized);

private:
    static int seen_call(sd_bus_message* call, void* self, sd_bus_error* error);
    static int done_call(sd_bus_message* call, void* self, sd_bus_error* error);
    static int surveyed_call(sd_bus_message* call, void* self, sd_bus_error* error);
    void surveyed(std::string_view report);
    void run(std::string source);
    void seen(std::string_view report);
    void command(std::string_view body);
    void on_titled(std::string_view title, std::string_view body);

    host::BusConnection& bus_;
    pid_t host_;
    std::string service_;
    sd_bus_slot* object_ = nullptr;
    std::unique_ptr<host::KWinScript> watcher_;
    std::vector<std::unique_ptr<host::KWinScript>> commands_;
    bool watching_ = false;
    std::optional<Seen> now_;
    std::vector<Seen> journal_;
    std::size_t done_ = 0;
    std::vector<Stacked> stacking_;
    std::size_t surveys_ = 0;
};

}
