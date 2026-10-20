#pragma once

struct wl_display;

namespace rwin::host {

class PollLoop;

// This client's connection to the compositor ($WAYLAND_DISPLAY), read and
// written by the loop: before every wait what is queued is dispatched and
// the requests made are flushed; after it, what came is read and
// dispatched. A connection the compositor ends - a protocol error, the
// compositor gone - ends the loop.
class WaylandDisplay {
public:
    // Throws std::runtime_error when there is no compositor to connect to.
    explicit WaylandDisplay(PollLoop& loop);
    ~WaylandDisplay();

    WaylandDisplay(const WaylandDisplay&) = delete;
    WaylandDisplay& operator=(const WaylandDisplay&) = delete;
    WaylandDisplay(WaylandDisplay&&) = delete;
    WaylandDisplay& operator=(WaylandDisplay&&) = delete;

    [[nodiscard]] wl_display* display() const noexcept { return display_; }

private:
    // What the connection is waited for: POLLIN, and POLLOUT while a flush
    // is short.
    short before_wait();
    void after_wait(bool readable);
    void check();

    PollLoop& loop_;
    wl_display* display_;
    bool reading_ = false;
};

}  // namespace rwin::host
