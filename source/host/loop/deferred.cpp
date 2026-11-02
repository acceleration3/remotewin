#include "host/loop/deferred.hpp"

#include <algorithm>

namespace rwin::host {

Deferred::Deferred(EventLoop& loop) : timer_(loop.timer([this] { fire(); })) {}

void Deferred::after(std::chrono::milliseconds delay, std::function<void()> action) {
    pending_.push_back(Pending{Clock::now() + delay, std::move(action)});
    arm();
}

void Deferred::clear() noexcept {
    pending_.clear();
    timer_->stop();
}

void Deferred::fire() {
    const Clock::time_point now = Clock::now();
    firing_.clear();
    const auto due = std::stable_partition(pending_.begin(), pending_.end(),
                                           [now](const Pending& pending) { return pending.due > now; });
    std::move(due, pending_.end(), std::back_inserter(firing_));
    pending_.erase(due, pending_.end());
    for (Pending& pending : firing_) pending.action();
    firing_.clear();
    arm();
}

void Deferred::arm() {
    if (pending_.empty()) {
        timer_->stop();
        return;
    }
    const auto next = std::min_element(pending_.begin(), pending_.end(),
                                       [](const Pending& a, const Pending& b) { return a.due < b.due; });
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(next->due - Clock::now());
    timer_->start(std::max(wait, std::chrono::milliseconds::zero()), false);
}

}
