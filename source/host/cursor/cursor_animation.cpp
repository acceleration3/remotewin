#include "host/cursor/cursor_animation.hpp"

#include <algorithm>
#include <utility>

namespace rwin::host {

CursorAnimation::CursorAnimation(QObject* parent) : QObject(parent) {
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &CursorAnimation::advance);
}

void CursorAnimation::play(std::vector<int> delays_ms) {
    timer_.stop();
    delays_ = std::move(delays_ms);
    step_ = 0;
    taken_ = 0;
    if (delays_.size() < 2) return;
    timer_.start(std::max(delays_.front(), kShortestMs));
}

void CursorAnimation::advance() {
    if (delays_.size() < 2) return;
    step_ = (step_ + 1) % delays_.size();
    ++taken_;
    timer_.start(std::max(delays_[step_], kShortestMs));
    Q_EMIT stepped(step_);
}

}  // namespace rwin::host
