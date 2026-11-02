#pragma once

struct wl_display;

namespace rwin::host {

class PollLoop;

class WaylandDisplay {
public:
    explicit WaylandDisplay(PollLoop& loop);
    ~WaylandDisplay();

    WaylandDisplay(const WaylandDisplay&) = delete;
    WaylandDisplay& operator=(const WaylandDisplay&) = delete;
    WaylandDisplay(WaylandDisplay&&) = delete;
    WaylandDisplay& operator=(WaylandDisplay&&) = delete;

    [[nodiscard]] wl_display* display() const noexcept { return display_; }

private:
    short before_wait();
    void after_wait(bool readable);
    void check();

    PollLoop& loop_;
    wl_display* display_;
    bool reading_ = false;
};

}
