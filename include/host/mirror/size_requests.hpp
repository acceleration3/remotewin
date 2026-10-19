#pragma once

#include "host/geometry/size.hpp"
#include "host/loop/event_loop.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace rwin::host {

// Sizes asked of the guest for a window, one at a time.
//
// The compositor says a new size for every step of a drag, tens of them a
// second, and a guest application laying itself out for each falls behind
// and answers sizes the user has long since dragged past. So the newest
// size waits for the last one to be answered, and the ones between are
// never asked for at all. A size the guest cannot take leaves its window as
// it was, which it says nothing of: the wait ends after a while regardless.
class SizeRequests {
public:
    SizeRequests(EventLoop& loop, std::function<void(Size)> ask);

    // The window is to be this large, in its pixels.
    void want(Size picture);
    // The guest says the window is this large now.
    void reported(Size picture);
    [[nodiscard]] bool waiting() const noexcept { return awaiting_.has_value(); }

private:
    void settle(Size reported);

    // A guest application lays a window out in tens of milliseconds; one
    // that has not answered in this long is not going to.
    static constexpr std::chrono::milliseconds kAnswerWait{500};

    std::function<void(Size)> ask_;
    std::unique_ptr<EventLoop::Timer> answer_due_;
    std::optional<Size> awaiting_;
    std::optional<Size> pending_;
    Size last_reported_;
};

}  // namespace rwin::host
