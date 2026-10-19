#pragma once

#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace rwin::host {

// The manager that spawned this (--managed): lines out on the status
// descriptor (LISTENING <port>, READY, APP <aumid>) and commands in on the
// command one (RAISE, optionally with the xdg-activation token it was
// handed). Both are borrowed from the spawning application's pipes.
class ParentControl {
public:
    ParentControl(EventLoop& loop, int command_fd, int status_fd);

    void report_listening(std::uint16_t port);
    void report_ready();
    // The application is known by this AppUserModelID; the manager groups
    // its mirrors by it.
    void report_app(std::string_view aumid);

    // RAISE; the token, empty when none came with it.
    Signal<const std::string&> raise_requested;
    // The parent closed its end: this mirror detaches.
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

}  // namespace rwin::host
