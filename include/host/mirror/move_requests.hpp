#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"
#include "host/loop/event_loop.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

namespace rwin::host {

// What one guest window is asked to be (WindowMove), one ask at a time: the
// only sender of a window's moves, so a size and a place asked for apart
// never reach the guest as two moves each undoing the other.
//
// The compositor says a new size for every step of a drag, tens of them a
// second, and a guest application laying itself out for each falls behind
// and answers sizes the user has long since dragged past. So the newest
// size and place wait for the last ask to be answered - the guest saying
// its window's frame changed - and are asked together; the ones between are
// never asked at all. A guest slower than kAnswerWait is asked the newest
// regardless, at most that often, and an answer to an older ask landing
// late is not taken for the guest's answer to the newest.
//
// Frames are the guest's: its screen's pixels, the DWM frame.
class MoveRequests {
public:
    using Ask = std::function<void(const Box& frame)>;
    // The newest ask was answered: what was asked, and the frame the guest
    // then had - another than asked when the guest would not do it (its
    // smallest size) or did something else meanwhile.
    using Answered = std::function<void(const Box& asked, const Box& got)>;

    MoveRequests(EventLoop& loop, Ask ask, Answered answered);

    // The frame as the guest last said it.
    void reported(const Box& frame);
    // The frame is to be this large. ``moving`` are the edges that move to
    // make it so (ResizeBorder's bits); the others stay where they are.
    void want_size(Size frame, std::uint32_t moving);
    // The frame's corner is to be here.
    void want_corner(Point corner);
    // Nothing asked and unanswered, nothing waiting to be asked.
    [[nodiscard]] bool idle() const noexcept {
        return outstanding_.empty() && !size_.has_value() && !corner_.has_value();
    }

private:
    // A guest application lays a window out in tens of milliseconds; one
    // that has not answered in this long is not going to.
    static constexpr std::chrono::milliseconds kAnswerWait{250};
    // Asks kept for their answers to be told apart by; a guest this far
    // behind answers none of them in time to matter.
    static constexpr std::size_t kMostOutstanding = 8;

    void ask_next();
    void overdue();
    void settle(const Box& asked);

    Ask ask_;
    Answered answered_;
    std::unique_ptr<EventLoop::Timer> answer_due_;
    Box reported_;
    // Asked and not seen to land, oldest first.
    std::deque<Box> outstanding_;
    Box reported_when_asked_;
    std::optional<Size> size_;
    std::uint32_t moving_ = 0;
    std::optional<Point> corner_;
};

}  // namespace rwin::host
