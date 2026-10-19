#include "host/wayland/wayland_display.hpp"

#include "host/loop/poll_loop.hpp"
#include "logging/logger.hpp"

#include <wayland-client.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace rwin::host {

WaylandDisplay::WaylandDisplay(PollLoop& loop) : loop_(loop), display_(wl_display_connect(nullptr)) {
    if (display_ == nullptr) {
        throw std::runtime_error(std::string("no Wayland compositor to connect to (WAYLAND_DISPLAY): ")
                                 + std::strerror(errno));
    }
    loop_.wrap_waits(
        wl_display_get_fd(display_), [this] { before_wait(); }, [this](bool readable) { after_wait(readable); });
}

WaylandDisplay::~WaylandDisplay() {
    loop_.wrap_waits(-1, {}, {});
    wl_display_disconnect(display_);
}

void WaylandDisplay::before_wait() {
    while (wl_display_prepare_read(display_) != 0) {
        if (wl_display_dispatch_pending(display_) < 0) {
            check();
            return;
        }
    }
    reading_ = true;
    // EAGAIN: the socket is full; what is left goes after the next wait.
    if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(display_);
        reading_ = false;
        check();
    }
}

void WaylandDisplay::after_wait(bool readable) {
    if (!reading_) return;
    reading_ = false;
    if (readable) {
        if (wl_display_read_events(display_) < 0) {
            check();
            return;
        }
    } else {
        wl_display_cancel_read(display_);
    }
    if (wl_display_dispatch_pending(display_) < 0) check();
}

void WaylandDisplay::check() {
    const int error = wl_display_get_error(display_);
    if (error == 0) return;
    const wl_interface* interface = nullptr;
    std::uint32_t id = 0;
    const std::uint32_t code = wl_display_get_protocol_error(display_, &interface, &id);
    if (error == EPROTO) {
        RWIN_ERROR("wayland", "the compositor ended the connection: protocol error {} on {}@{}", code,
                   interface != nullptr ? interface->name : "?", id);
    } else {
        RWIN_ERROR("wayland", "the connection to the compositor failed: {}", std::strerror(error));
    }
    loop_.quit(EXIT_FAILURE);
}

}  // namespace rwin::host
