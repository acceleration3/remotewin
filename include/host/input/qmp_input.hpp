#pragma once

#include "host/export/unique_fd.hpp"
#include "host/loop/event_loop.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace rwin::host {

class QmpInput {
public:
    QmpInput(EventLoop& loop, std::string path);
    ~QmpInput();

    QmpInput(const QmpInput&) = delete;
    QmpInput& operator=(const QmpInput&) = delete;
    QmpInput(QmpInput&&) = delete;
    QmpInput& operator=(QmpInput&&) = delete;

    [[nodiscard]] bool ready() const noexcept { return state_ == State::Ready; }

    void move_to(int x, int y, int width, int height);
    [[nodiscard]] bool button(std::uint32_t evdev_code, bool pressed);
    void wheel(std::int32_t dx120, std::int32_t dy120);
    [[nodiscard]] bool key(std::uint32_t evdev_code, bool pressed);
    void release_keys();

private:
    enum class State { Closed, Greeting, Negotiating, Ready };

    void connect();
    void drop(const char* why);
    void retry_later();
    void on_ready(short events);
    void read_replies();
    void take_line(const std::string& line);
    void write_pending();
    void send_line(const std::string& line);
    void want_events();
    void queue(std::string event);
    void flush();
    void notch(const char* button);

    EventLoop& loop_;
    std::string path_;
    UniqueFd socket_;
    std::unique_ptr<EventLoop::Watch> watch_;
    std::unique_ptr<EventLoop::Timer> reconnect_;
    std::unique_ptr<EventLoop::Timer> batch_;
    std::chrono::milliseconds backoff_;
    State state_ = State::Closed;
    bool reported_unavailable_ = false;
    std::string incoming_;
    std::string outgoing_;
    std::vector<std::string> events_;
    bool last_is_motion_ = false;
    std::set<std::uint16_t> held_;
    std::int32_t owed_x_ = 0;
    std::int32_t owed_y_ = 0;
};

}
