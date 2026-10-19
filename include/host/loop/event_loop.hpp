#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace rwin::host {

// What the host side waits on: file descriptors becoming ready, and time
// passing. remotewin runs a loop of its own (PollLoop); a program that has
// one already - LSW's manager, on Qt's - gives that one.
class EventLoop {
public:
    // A file descriptor, watched for as long as this lives.
    class Watch {
    public:
        virtual ~Watch() = default;
        // POLLIN, POLLOUT, or both, from now on.
        virtual void want(short events) = 0;
    };

    class Timer {
    public:
        virtual ~Timer() = default;
        // Fires once ``after`` from now, or every ``after`` with ``repeat``;
        // a timer started again starts over.
        virtual void start(std::chrono::milliseconds after, bool repeat) = 0;
        virtual void stop() = 0;
        [[nodiscard]] virtual bool active() const = 0;
    };

    // Called with what the descriptor became ready for (poll's revents).
    using Ready = std::function<void(short)>;

    virtual ~EventLoop() = default;

    [[nodiscard]] virtual std::unique_ptr<Watch> watch(int fd, short events, Ready ready) = 0;
    [[nodiscard]] virtual std::unique_ptr<Timer> timer(std::function<void()> fired) = 0;
};

}  // namespace rwin::host
