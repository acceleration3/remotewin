#pragma once

#include "host/loop/event_loop.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <poll.h>

namespace rwin::host {

// remotewin's own loop: poll(2) over every descriptor watched, its timeout
// the nearest timer.
//
// One descriptor is special: a Wayland connection is read by preparing to
// read before the wait and either reading or cancelling after it, which a
// plain watch cannot say (wrap_waits).
class PollLoop final : public EventLoop {
public:
    PollLoop() = default;
    ~PollLoop() override;

    PollLoop(const PollLoop&) = delete;
    PollLoop& operator=(const PollLoop&) = delete;
    PollLoop(PollLoop&&) = delete;
    PollLoop& operator=(PollLoop&&) = delete;

    [[nodiscard]] std::unique_ptr<Watch> watch(int fd, short events, Ready ready) override;
    [[nodiscard]] std::unique_ptr<Timer> timer(std::function<void()> fired) override;

    // ``before`` runs before every wait and says what ``fd`` is waited for
    // (POLLIN, and POLLOUT while there is more to write); ``after`` runs
    // after it, told whether ``fd`` became readable.
    void wrap_waits(int fd, std::function<short()> before, std::function<void(bool)> after);

    // Until quit; the status quit was given.
    int run();
    void quit(int status) noexcept;

private:
    using Clock = std::chrono::steady_clock;
    class PollWatch;
    class PollTimer;
    struct Watched {
        int fd = -1;
        short events = 0;
        Ready ready;
    };

    void forget_watch(std::uint64_t id) noexcept;
    void forget_timer(PollTimer* timer) noexcept;
    [[nodiscard]] int timeout_ms() const;
    void fire_timers();
    void wait_once();

    std::map<std::uint64_t, Watched> watched_;
    std::uint64_t next_watch_ = 0;
    std::vector<PollTimer*> timers_;
    std::vector<PollTimer*> due_;
    int wrapped_fd_ = -1;
    std::function<short()> before_wait_;
    std::function<void(bool)> after_wait_;
    // Reused wait to wait.
    std::vector<pollfd> polled_;
    std::vector<std::uint64_t> polled_ids_;
    bool running_ = false;
    int status_ = 0;
};

}  // namespace rwin::host
