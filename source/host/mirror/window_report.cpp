#include "host/mirror/window_report.hpp"

#include "host/chrome/win32_style.hpp"
#include "host/dbus/bus_connection.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "host/mirror/json_text.hpp"
#include "host/mirror/mirror_session.hpp"
#include "logging/logger.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <systemd/sd-bus.h>

#include <unistd.h>

#include <format>

namespace rwin::host {

namespace {

constexpr const char* kPath = "/org/remotewin/Windows";
constexpr const char* kInterface = "org.remotewin.Windows";

// "0x30a2c:7", as the logs name it; null for none.
void name(JsonText& json, WindowId window) {
    if (window.none()) {
        json.null();
    } else {
        json.value(std::format("{}", window));
    }
}

void box(JsonText& json, const Box& area) {
    json.open_array();
    for (const int value : {area.x, area.y, area.width, area.height}) json.value(static_cast<std::int64_t>(value));
    json.close_array();
}

const char* show_name(WindowFields::Show show) {
    switch (show) {
        case WindowFields::Show::Hidden: return "hidden";
        case WindowFields::Show::Minimized: return "minimized";
        case WindowFields::Show::Maximized: return "maximized";
        case WindowFields::Show::Shown: return "shown";
    }
    return "unknown";
}

void guest_of(JsonText& json, const GuestWindow& window, const MirrorSession& session) {
    const Win32Style style{window};
    const WindowFields& fields = window.fields();
    json.open_object();
    json.key("owner");
    name(json, fields.owner);
    json.key("anchor");
    name(json, window.anchor());
    json.key("popup_kind").value(static_cast<std::int64_t>(fields.popup.kind));
    json.key("pid").value(static_cast<std::int64_t>(fields.app_id.process_id));
    json.key("aumid").value(fields.app_id.aumid);
    json.key("image").value(fields.app_id.image);
    json.key("belongs").value(session.belongs(window.id()));
    json.key("frame");
    box(json, window.frame());
    json.key("rect");
    box(json, Box::of(window.picture_origin(), window.picture_size()));
    json.key("style").value(std::format("{:#x}", fields.style.style));
    json.key("ex_style").value(std::format("{:#x}", fields.style.extended));
    json.key("title").value(fields.title);
    json.key("class").value(fields.class_name);
    json.key("show").value(show_name(fields.show));
    json.key("active").value(session.windows().order().active == window.id());
    json.key("band").value(static_cast<std::int64_t>(fields.band));
    json.key("in_taskbar").value(style.in_taskbar());
    json.key("owned").value(style.owned());
    json.key("stands_alone").value(style.stands_alone());
    json.key("activates").value(style.activates());
    json.key("topmost").value(style.topmost());
    json.key("disabled").value(style.disabled());
    json.key("tool_window").value(style.tool_window());
    json.close_object();
}

void mirror_of(JsonText& json, const MirrorState& state) {
    json.open_object();
    json.key("role").value(name_of(state.role.kind));
    json.key("parent");
    name(json, state.role.parent);
    json.key("shown").value(state.shown);
    json.key("mapped").value(state.mapped);
    json.key("activated").value(state.activated);
    json.key("maximized").value(state.maximized);
    json.key("fullscreen").value(state.fullscreen);
    json.key("asked_minimized").value(state.asked_minimized);
    json.key("asked_maximized").value(state.asked_maximized);
    json.key("asked_fullscreen").value(state.asked_fullscreen);
    json.key("modal").value(state.modal);
    json.key("grabs").value(state.grabs);
    json.key("placed");
    box(json, state.placed);
    json.key("title").value(state.title);
    json.key("logical_size");
    box(json, Box::of(Point{}, state.logical));
    json.key("scale").value(state.scale);
    json.key("picture");
    box(json, Box::of(Point{}, state.picture));
    json.key("frames_presented").value(static_cast<std::int64_t>(state.frames));
    json.key("surfaces_made").value(static_cast<std::int64_t>(state.generation));
    json.close_object();
}

}  // namespace

WindowReport::WindowReport(BusConnection& bus, const MirrorSession& session) : session_(session) {
    if (!bus.connected()) return;
    static const sd_bus_vtable kVtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Describe", "", "s", &WindowReport::described, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };
    if (sd_bus_add_object_vtable(bus.bus(), &object_, kPath, kInterface, kVtable, this) < 0) {
        RWIN_WARN("report", "cannot put the window report on the session bus");
        return;
    }
    RWIN_INFO("report", "window report at {} {}", bus.unique_name(), kPath);
}

WindowReport::~WindowReport() {
    if (object_ != nullptr) sd_bus_slot_unref(object_);
}

int WindowReport::described(sd_bus_message* call, void* self, sd_bus_error* /*error*/) {
    const std::string report = static_cast<WindowReport*>(self)->describe();
    return sd_bus_reply_method_return(call, "s", report.c_str());
}

std::string WindowReport::describe() const {
    JsonText json;
    json.open_object();
    json.key("host_pid").value(static_cast<std::int64_t>(::getpid()));
    json.key("app_id").value(session_.app_id());
    json.key("windows").open_array();
    for (const GuestWindow* window : session_.windows().stacked()) {
        json.open_object();
        json.key("handle");
        name(json, window->id());
        json.key("guest");
        guest_of(json, *window, session_);
        json.key("mirror");
        if (const std::optional<MirrorState> state = session_.state_of(window->id())) {
            mirror_of(json, *state);
        } else {
            json.null();
        }
        json.close_object();
    }
    json.close_array();
    json.close_object();
    return json.text();
}

}  // namespace rwin::host
