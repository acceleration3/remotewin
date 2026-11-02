#pragma once

#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace rwin::host {
class ParentControl {
public:
    ParentControl(EventLoop& loop, int command_fd, int status_fd);

    void report_listening(std::uint16_t port);
    void report_ready();
    void report_app(std::string_view aumid);

    Signal<const std::string&> raise_requested;
    Signal<> closed;
    Signal<> failed;

private:
    void read_commands();
    void report(std::string_view line);

    int command_fd_;
    int status_fd_;
    std::unique_ptr<EventLoop::Watch> commands_;
    std::string pending_;
};

}
