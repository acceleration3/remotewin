#include "kwin_view.hpp"

#include "host/dbus/bus_connection.hpp"
#include "host/latency/frame_stamps.hpp"
#include "host/placement/kwin_script.hpp"

#include <systemd/sd-bus.h>

#include <charconv>
#include <cstdio>
#include <format>

namespace rwin::integration {

namespace {

constexpr const char* kPath = "/remotewin/integration";
constexpr const char* kInterface = "org.remotewin.Integration";

// $SERVICE, $PATH, $INTERFACE, $PID are put in before the script runs.
constexpr std::string_view kWatcher = R"JS(
function report(w, what) {
    const g = w.frameGeometry;
    callDBus("$SERVICE", "$PATH", "$INTERFACE", "seen",
             [what, g.x, g.y, g.width, g.height, w.maximizeMode, w.minimized ? 1 : 0, w.resize ? 1 : 0].join(" "));
}
function watch(w) {
    if (w.pid !== $PID || !w.normalWindow) return;
    w.frameGeometryChanged.connect(function() { report(w, "frame"); });
    w.maximizedChanged.connect(function() { report(w, "maximized"); });
    w.minimizedChanged.connect(function() { report(w, "minimized"); });
    w.interactiveMoveResizeStarted.connect(function() { report(w, "drag-start"); });
    w.interactiveMoveResizeFinished.connect(function() { report(w, "drag-end"); });
    w.closed.connect(function() { report(w, "closed"); });
    report(w, "added");
}
workspace.windowList().forEach(watch);
workspace.windowAdded.connect(watch);
)JS";

constexpr std::string_view kCommand = R"JS(
workspace.windowList().forEach(function(w) {
    if (w.pid !== $PID || !w.normalWindow) return;
    $BODY
});
callDBus("$SERVICE", "$PATH", "$INTERFACE", "done", "");
)JS";

void replace_all(std::string& text, std::string_view name, std::string_view value) {
    for (std::size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + value.size())) {
        text.replace(at, name.size(), value);
    }
}

std::vector<std::string_view> words_of(std::string_view text) {
    std::vector<std::string_view> words;
    while (!text.empty()) {
        const std::size_t end = text.find(' ');
        if (end != 0) words.push_back(text.substr(0, end));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return words;
}

double number(std::string_view text) {
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

int string_call(sd_bus_message* call, void* self, void (KWinView::*method)(std::string_view)) {
    const char* text = nullptr;
    if (sd_bus_message_read(call, "s", &text) < 0 || text == nullptr) {
        return sd_bus_reply_method_errorf(call, SD_BUS_ERROR_INVALID_ARGS, "one string");
    }
    (static_cast<KWinView*>(self)->*method)(text);
    return sd_bus_reply_method_return(call, "");
}

}  // namespace

KWinView::KWinView(host::EventLoop& /*loop*/, host::BusConnection& bus, pid_t host)
    : bus_(bus), host_(host), service_(bus.unique_name()) {
    static const sd_bus_vtable kVtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("seen", "s", "", &KWinView::seen_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("done", "s", "", &KWinView::done_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };
    if (sd_bus_add_object_vtable(bus.bus(), &object_, kPath, kInterface, kVtable, this) < 0) {
        std::fprintf(stderr, "kwin: cannot put the report object on the bus\n");
        return;
    }
    std::string source{kWatcher};
    replace_all(source, "$SERVICE", service_);
    replace_all(source, "$PATH", kPath);
    replace_all(source, "$INTERFACE", kInterface);
    replace_all(source, "$PID", std::to_string(host_));
    watcher_ = std::make_unique<host::KWinScript>(bus_, "remotewin-integration-watch", source,
                                                  [this](bool ran, const std::string& error) {
                                                      watching_ = ran;
                                                      if (!ran) std::fprintf(stderr, "kwin: %s\n", error.c_str());
                                                  });
}

KWinView::~KWinView() {
    commands_.clear();
    watcher_.reset();
    if (object_ != nullptr) sd_bus_slot_unref(object_);
}

int KWinView::seen_call(sd_bus_message* call, void* self, sd_bus_error* /*error*/) {
    return string_call(call, self, &KWinView::seen);
}

int KWinView::done_call(sd_bus_message* call, void* self, sd_bus_error* /*error*/) {
    ++static_cast<KWinView*>(self)->done_;
    return sd_bus_reply_method_return(call, "");
}

void KWinView::seen(std::string_view report) {
    const std::vector<std::string_view> parts = words_of(report);
    if (parts.size() != 8) return;
    Seen seen{host::now_ns(), std::string(parts[0]), number(parts[1]), number(parts[2]), number(parts[3]),
              number(parts[4]), static_cast<int>(number(parts[5])), parts[6] == "1", parts[7] == "1"};
    if (seen.what != "closed") now_ = seen;
    journal_.push_back(std::move(seen));
}

void KWinView::command(std::string_view body) {
    std::string source{kCommand};
    replace_all(source, "$BODY", body);
    replace_all(source, "$SERVICE", service_);
    replace_all(source, "$PATH", kPath);
    replace_all(source, "$INTERFACE", kInterface);
    replace_all(source, "$PID", std::to_string(host_));
    commands_.push_back(std::make_unique<host::KWinScript>(
        bus_, std::format("remotewin-integration-command-{}", commands_.size()), source,
        [](bool ran, const std::string& error) {
            if (!ran) std::fprintf(stderr, "kwin: a command did not run: %s\n", error.c_str());
        }));
}

void KWinView::set_frame(double x, double y, double width, double height) {
    command(std::format("w.frameGeometry = {{x: {}, y: {}, width: {}, height: {}}};", x, y, width, height));
}

void KWinView::maximize(bool maximized) {
    command(maximized ? "w.setMaximize(true, true);" : "w.setMaximize(false, false);");
}

void KWinView::minimize(bool minimized) {
    command(minimized ? "w.minimized = true;" : "w.minimized = false; workspace.activeWindow = w;");
}

}  // namespace rwin::integration
