#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace rwin::host {
class EventLoop {
public:
    class Watch {
    public:
        virtual ~Watch() = default;
        virtual void want(short events) = 0;
    };

    class Timer {
    public:
        virtual ~Timer() = default;
        virtual void start(std::chrono::milliseconds after, bool repeat) = 0;
        virtual void stop() = 0;
        [[nodiscard]] virtual bool active() const = 0;
    };

    using Ready = std::function<void(short)>;

    virtual ~EventLoop() = default;

    [[nodiscard]] virtual std::unique_ptr<Watch> watch(int fd, short events, Ready ready) = 0;
    [[nodiscard]] virtual std::unique_ptr<Timer> timer(std::function<void()> fired) = 0;
};

}
