#include "host/mirror/size_requests.hpp"

namespace rwin::host {

SizeRequests::SizeRequests(EventLoop& loop, std::function<void(Size)> ask)
    : ask_(std::move(ask)), answer_due_(loop.timer([this] { settle(last_reported_); })) {}

void SizeRequests::want(Size picture) {
    if (picture.empty()) return;
    if (awaiting_.has_value()) {
        pending_ = picture;
        return;
    }
    if (picture == last_reported_) return;
    awaiting_ = picture;
    answer_due_->start(kAnswerWait, false);
    ask_(picture);
}

void SizeRequests::reported(Size picture) {
    last_reported_ = picture;
    settle(picture);
}

// Any answer ends the wait: the guest applies what it is sent in order, so
// a report is the end of the last request whatever size it came out as.
void SizeRequests::settle(Size reported) {
    if (!awaiting_.has_value()) return;
    answer_due_->stop();
    awaiting_.reset();
    if (!pending_.has_value()) return;
    const Size next = *pending_;
    pending_.reset();
    if (next != reported) want(next);
}

}  // namespace rwin::host
