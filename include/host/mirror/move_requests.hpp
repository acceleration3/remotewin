#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"
#include "host/loop/event_loop.hpp"

#include <chrono>
#include <cstdint>
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
// never asked at all. A guest that cannot do what it was asked says
// nothing of it: the wait ends after kAnswerWait regardless.
//
// Frames are the guest's: its screen's pixels, the DWM frame.
class MoveRequests {
public:
    using Ask = std::function<void(const Box& frame)>;
    // An ask was answered: what was asked, and the frame the guest then had.
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
        return !asked_.has_value() && !size_.has_value() && !corner_.has_value();
    }

private:
    // A guest application lays a window out in tens of milliseconds; one
    // that has not answered in this long is not going to.
    static constexpr std::chrono::milliseconds kAnswerWait{250};

    void ask_next();
    void settle();

    Ask ask_;
    Answered answered_;
    std::unique_ptr<EventLoop::Timer> answer_due_;
    Box reported_;
    std::optional<Box> asked_;
    Box reported_when_asked_;
    std::optional<Size> size_;
    std::uint32_t moving_ = 0;
    std::optional<Point> corner_;
};

}  // namespace rwin::host
