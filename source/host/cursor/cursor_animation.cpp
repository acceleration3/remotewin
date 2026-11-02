#include "host/cursor/cursor_animation.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace rwin::host {

CursorAnimation::CursorAnimation(EventLoop& loop) : timer_(loop.timer([this] { advance(); })) {}

void CursorAnimation::play(std::vector<int> delays_ms) {
    timer_->stop();
    delays_ = std::move(delays_ms);
    step_ = 0;
    taken_ = 0;
    if (delays_.size() < 2) return;
    timer_->start(std::chrono::milliseconds(std::max(delays_.front(), kShortestMs)), false);
}

void CursorAnimation::advance() {
    if (delays_.size() < 2) return;
    step_ = (step_ + 1) % delays_.size();
    ++taken_;
    timer_->start(std::chrono::milliseconds(std::max(delays_[step_], kShortestMs)), false);
    stepped(step_);
}

}
