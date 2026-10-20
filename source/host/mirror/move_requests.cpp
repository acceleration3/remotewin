#include "host/mirror/move_requests.hpp"

#include "host/chrome/resize_border.hpp"

#include <utility>

namespace rwin::host {

MoveRequests::MoveRequests(EventLoop& loop, Ask ask, Answered answered)
    : ask_(std::move(ask)), answered_(std::move(answered)), answer_due_(loop.timer([this] { settle(); })) {}

// The guest applies what it is sent in order, so any change to the frame is
// the end of the last ask, whatever it came out as; a report that leaves
// the frame as it was (a new title) is not.
void MoveRequests::reported(const Box& frame) {
    reported_ = frame;
    if (asked_.has_value() && frame != reported_when_asked_) settle();
}

void MoveRequests::want_size(Size frame, std::uint32_t moving) {
    if (frame.empty()) return;
    size_ = frame;
    moving_ = moving;
    if (!asked_.has_value()) ask_next();
}

void MoveRequests::want_corner(Point corner) {
    corner_ = corner;
    if (!asked_.has_value()) ask_next();
}

// Measured from the frame the guest has now: the last ask's answer.
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
    if (target == reported_) return;
    asked_ = target;
    reported_when_asked_ = reported_;
    answer_due_->start(kAnswerWait, false);
    ask_(target);
}

void MoveRequests::settle() {
    if (!asked_.has_value()) return;
    answer_due_->stop();
    const Box asked = *asked_;
    asked_.reset();
    if (size_.has_value() || corner_.has_value()) ask_next();
    answered_(asked, reported_);
}

}  // namespace rwin::host
