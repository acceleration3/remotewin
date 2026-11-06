#include "guest/windows/maximized_position.hpp"

#include <cstdlib>

namespace rwin::guest {

MaximizedPosition::Spot MaximizedPosition::centred_over(const Rect& window, const Rect& work) {
    const auto excess_width = static_cast<std::int64_t>(window.width) - static_cast<std::int64_t>(work.width);
    const auto excess_height = static_cast<std::int64_t>(window.height) - static_cast<std::int64_t>(work.height);
    return Spot{static_cast<std::int32_t>(work.x - excess_width / 2),
                static_cast<std::int32_t>(work.y - excess_height / 2)};
}

std::optional<MaximizedPosition::Spot> MaximizedPosition::correction(void* window, bool zoomed, const Rect& rect,
                                                                     const Rect& work, Clock::time_point now) {
    if (!zoomed) {
        forget(window);
        return std::nullopt;
    }
    const Spot right = centred_over(rect, work);
    if (std::abs(rect.x - right.x) <= kTolerance && std::abs(rect.y - right.y) <= kTolerance) return std::nullopt;
    Corrected& done = corrected_[window];
    if (done.times >= kMostPerMaximize || (done.times > 0 && now - done.last < kLeastBetween)) return std::nullopt;
    ++done.times;
    done.last = now;
    return right;
}

}
