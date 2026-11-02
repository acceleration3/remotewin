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
class MoveRequests {
public:
    using Ask = std::function<void(const Box& frame)>;
    using Answered = std::function<void(const Box& asked, const Box& got)>;

    MoveRequests(EventLoop& loop, Ask ask, Answered answered);

    void reported(const Box& frame);
    void want_size(Size frame, std::uint32_t moving);
    void want_corner(Point corner);
    [[nodiscard]] bool idle() const noexcept {
        return outstanding_.empty() && !size_.has_value() && !corner_.has_value();
    }

private:
    static constexpr std::chrono::milliseconds kAnswerWait{250};
    static constexpr std::size_t kMostOutstanding = 8;

    void ask_next();
    void overdue();
    void settle(const Box& asked);

    Ask ask_;
    Answered answered_;
    std::unique_ptr<EventLoop::Timer> answer_due_;
    Box reported_;
    std::deque<Box> outstanding_;
    Box reported_when_asked_;
    std::optional<Size> size_;
    std::uint32_t moving_ = 0;
    std::optional<Point> corner_;
};

}
