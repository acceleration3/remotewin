#pragma once

namespace rwin::host {

struct ToplevelConfigure;

// What a toplevel's owner hears from the compositor.
class ToplevelEvents {
public:
    // Acknowledged already; what is committed next is the answer.
    virtual void configured(const ToplevelConfigure& configure) = 0;
    // The user asked the window to close: the taskbar, Alt+F4.
    virtual void close_requested() = 0;

protected:
    ~ToplevelEvents() = default;
};

}  // namespace rwin::host
