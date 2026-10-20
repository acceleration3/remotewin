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
}  // namespace rwin::host

namespace rwin::integration {

// The test compositor's own view of the mirror under test, and its hands
// on it, through KWin's scripting on the harness's private session bus: a
// script inside KWin reports every change of the windows of the host's
// process - frame, maximise mode, minimised, an interactive move or resize
// starting and ending - and one-shot scripts do to them what a user or
// the window manager would.
class KWinView {
public:
    // One report, as KWin had the window then. Logical units.
    struct Seen {
        std::int64_t at_ns = 0;
        std::string what;
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
        // KWin's MaximizeMode: 0 restored, 3 maximised both ways.
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

    // One of the host's windows as KWin stacks them, any kind - a popup, a
    // dialog - with its title less the host's invisible tag.
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
    // How the host's window last was; nothing before KWin has it.
    [[nodiscard]] const std::optional<Seen>& now() const noexcept { return now_; }
    [[nodiscard]] const std::vector<Seen>& journal() const noexcept { return journal_; }
    // How many commands KWin has carried out.
    [[nodiscard]] std::size_t done() const noexcept { return done_; }

    // Asks KWin for the host's windows bottom to top (workspace.stackingOrder);
    // the answer is stacking() once surveys() has gone up by one.
    void survey();
    [[nodiscard]] const std::vector<Stacked>& stacking() const noexcept { return stacking_; }
    [[nodiscard]] std::size_t surveys() const noexcept { return surveys_; }
    // Makes the host's window of that title the active one, as Alt+Tab or the
    // taskbar would.
    void activate(const std::string& title);
    // Closes the host's window of that title, as its close button would.
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

}  // namespace rwin::integration
