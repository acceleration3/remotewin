#include "host/mirror/frame_dump_request.hpp"

#include "host/dbus/bus_connection.hpp"
#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"
#include "proto/window_id.hpp"

#include <systemd/sd-bus.h>

#include <cstdint>

namespace rwin::host {

namespace {

constexpr const char* kPath = "/org/remotewin/FrameDump";
constexpr const char* kInterface = "org.remotewin.FrameDump";

}

FrameDumpRequest::FrameDumpRequest(BusConnection& bus, GuestLink& link) : link_(link) {
    if (!bus.connected()) return;
    static const sd_bus_vtable kVtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Dump", "uu", "", &FrameDumpRequest::dump_asked, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };
    if (sd_bus_add_object_vtable(bus.bus(), &object_, kPath, kInterface, kVtable, this) < 0) {
        RWIN_WARN("dump", "cannot put the frame dump request on the session bus");
    }
}

FrameDumpRequest::~FrameDumpRequest() {
    if (object_ != nullptr) sd_bus_slot_unref(object_);
}

int FrameDumpRequest::dump_asked(sd_bus_message* call, void* self, sd_bus_error* ) {
    std::uint32_t hwnd = 0;
    std::uint32_t serial = 0;
    const int read = sd_bus_message_read(call, "uu", &hwnd, &serial);
    if (read < 0) return read;
    const WindowId window{hwnd, serial};
    RWIN_INFO("dump", "the guest is asked to dump the next frame of {}", window);
    static_cast<FrameDumpRequest*>(self)->link_.send_dump_frame(window);
    return sd_bus_reply_method_return(call, "");
}

}
