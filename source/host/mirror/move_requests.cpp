#include "host/mirror/move_requests.hpp"

#include "host/chrome/resize_border.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace rwin::host {

MoveRequests::MoveRequests(EventLoop& loop, Ask ask, Answered answered)
    : ask_(std::move(ask)), answered_(std::move(answered)), answer_due_(loop.timer([this] { overdue(); })) {}

void MoveRequests::reported(const Box& frame) {
    reported_ = frame;
    if (outstanding_.empty()) return;
    const auto landed = std::find(outstanding_.begin(), outstanding_.end(), frame);
    if (landed != outstanding_.end()) {
        const bool newest = std::next(landed) == outstanding_.end();
        outstanding_.erase(outstanding_.begin(), std::next(landed));
        if (newest) settle(frame);
        return;
    }
    if (frame == reported_when_asked_) return;
    const Box newest = outstanding_.back();
    outstanding_.clear();
    settle(newest);
}

void MoveRequests::want_size(Size frame, std::uint32_t moving) {
    if (frame.empty()) return;
    size_ = frame;
    moving_ = moving;
    if (outstanding_.empty()) ask_next();
}

void MoveRequests::want_corner(Point corner) {
    corner_ = corner;
    if (outstanding_.empty()) ask_next();
}

void MoveRequests::ask_next() {
    Box target = reported_;
    if (size_.has_value()) {
        if ((moving_ & ResizeBorder::kLeft) != 0) target.x = reported_.right() - size_->width;
        if ((moving_ & ResizeBorder::kTop) != 0) target.y = reported_.bottom() - size_->height;
        target.width = size_->width;
        target.height = size_->height;
    }
    if (corner_.has_value()) {
        target.x = corner_->x;
        target.y = corner_->y;
    }
    size_.reset();
    corner_.reset();
    if (target == reported_ || (!outstanding_.empty() && target == outstanding_.back())) return;
    outstanding_.push_back(target);
    if (outstanding_.size() > kMostOutstanding) outstanding_.pop_front();
    reported_when_asked_ = reported_;
    answer_due_->start(kAnswerWait, false);
    ask_(target);
}

void MoveRequests::overdue() {
    if (size_.has_value() || corner_.has_value()) ask_next();
}

void MoveRequests::settle(const Box& asked) {
    answer_due_->stop();
    if (size_.has_value() || corner_.has_value()) ask_next();
    answered_(asked, reported_);
}

}
