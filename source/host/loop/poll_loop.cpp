#include "host/loop/poll_loop.hpp"

#include "logging/logger.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <optional>
#include <cstring>

#include <poll.h>

namespace rwin::host {

class PollLoop::PollWatch final : public EventLoop::Watch {
public:
    PollWatch(PollLoop& loop, std::uint64_t id) noexcept : loop_(loop), id_(id) {}
    ~PollWatch() override { loop_.forget_watch(id_); }

    PollWatch(const PollWatch&) = delete;
    PollWatch& operator=(const PollWatch&) = delete;
    PollWatch(PollWatch&&) = delete;
    PollWatch& operator=(PollWatch&&) = delete;

    void want(short events) override {
        const auto found = loop_.watched_.find(id_);
        if (found != loop_.watched_.end()) found->second.events = events;
    }

private:
    PollLoop& loop_;
    std::uint64_t id_;
};

class PollLoop::PollTimer final : public EventLoop::Timer {
public:
    PollTimer(PollLoop& loop, std::function<void()> fired) : loop_(loop), fired_(std::move(fired)) {
        loop_.timers_.push_back(this);
    }
    ~PollTimer() override { loop_.forget_timer(this); }

    PollTimer(const PollTimer&) = delete;
    PollTimer& operator=(const PollTimer&) = delete;
    PollTimer(PollTimer&&) = delete;
    PollTimer& operator=(PollTimer&&) = delete;

    void start(std::chrono::milliseconds after, bool repeat) override {
        period_ = repeat ? after : std::chrono::milliseconds::zero();
        due_ = Clock::now() + after;
        active_ = true;
    }
    void stop() override { active_ = false; }
    [[nodiscard]] bool active() const override { return active_; }

    [[nodiscard]] Clock::time_point due() const noexcept { return due_; }
    // Due by ``now``: rearmed or stopped, then fired.
    void fire() {
        if (period_.count() > 0) {
            due_ += period_;
            const Clock::time_point now = Clock::now();
            if (due_ < now) due_ = now + period_;
        } else {
            active_ = false;
        }
        fired_();
    }

private:
    PollLoop& loop_;
    std::function<void()> fired_;
    Clock::time_point due_{};
    std::chrono::milliseconds period_{0};
    bool active_ = false;
};

PollLoop::~PollLoop() {
    // Timers and watches still alive outlive nothing they were made by;
    // their loop going first only stops them being heard.
    for (PollTimer* timer : timers_) timer->stop();
}

std::unique_ptr<EventLoop::Watch> PollLoop::watch(int fd, short events, Ready ready) {
    const std::uint64_t id = ++next_watch_;
    watched_.emplace(id, Watched{fd, events, std::move(ready)});
    return std::make_unique<PollWatch>(*this, id);
}

std::unique_ptr<EventLoop::Timer> PollLoop::timer(std::function<void()> fired) {
    return std::make_unique<PollTimer>(*this, std::move(fired));
}

void PollLoop::wrap_waits(int fd, std::function<short()> before, std::function<void(bool)> after) {
    wrapped_fd_ = fd;
    before_wait_ = std::move(before);
    after_wait_ = std::move(after);
}

void PollLoop::forget_watch(std::uint64_t id) noexcept { watched_.erase(id); }

void PollLoop::forget_timer(PollTimer* timer) noexcept { std::erase(timers_, timer); }

int PollLoop::run() {
    running_ = true;
    while (running_) wait_once();
    return status_;
}

void PollLoop::quit(int status) noexcept {
    status_ = status;
    running_ = false;
}

int PollLoop::timeout_ms() const {
    const Clock::time_point now = Clock::now();
    std::optional<Clock::time_point> nearest;
    for (const PollTimer* timer : timers_) {
        if (!timer->active()) continue;
        if (!nearest.has_value() || timer->due() < *nearest) nearest = timer->due();
    }
    if (!nearest.has_value()) return -1;
    if (*nearest <= now) return 0;
    // Rounded up: a timer is never fired before it is due and polled again.
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(*nearest - now).count();
    return static_cast<int>(std::min<long long>(wait, INT_MAX));
}

void PollLoop::wait_once() {
    const short wrapped_events = before_wait_ ? before_wait_() : POLLIN;
    if (!running_) return;
    polled_.clear();
    polled_ids_.clear();
    if (wrapped_fd_ >= 0) {
        polled_.push_back(pollfd{wrapped_fd_, wrapped_events, 0});
        polled_ids_.push_back(0);
    }
    for (const auto& [id, watched] : watched_) {
        polled_.push_back(pollfd{watched.fd, watched.events, 0});
        polled_ids_.push_back(id);
    }
    const int ready = ::poll(polled_.data(), polled_.size(), timeout_ms());
    if (ready < 0 && errno != EINTR) {
        RWIN_ERROR("loop", "poll failed: {}", std::strerror(errno));
        quit(EXIT_FAILURE);
        return;
    }
    std::size_t first = 0;
    if (wrapped_fd_ >= 0) {
        if (after_wait_) after_wait_(ready > 0 && (polled_[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0);
        first = 1;
    }
    for (std::size_t at = first; ready > 0 && at < polled_.size(); ++at) {
        if (polled_[at].revents == 0) continue;
        // A callback may have let go of a later watch: it is not called.
        const auto found = watched_.find(polled_ids_[at]);
        if (found == watched_.end()) continue;
        const Ready ready_now = found->second.ready;
        ready_now(polled_[at].revents);
    }
    fire_timers();
}

void PollLoop::fire_timers() {
    const Clock::time_point now = Clock::now();
    due_.clear();
    for (PollTimer* timer : timers_) {
        if (timer->active() && timer->due() <= now) due_.push_back(timer);
    }
    for (PollTimer* timer : due_) {
        // One fired before may have let go of this one.
        if (std::find(timers_.begin(), timers_.end(), timer) == timers_.end() || !timer->active()) continue;
        timer->fire();
    }
}

}  // namespace rwin::host
