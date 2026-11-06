#pragma once

#include "proto/rect.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace rwin::guest {

class MaximizedPosition {
public:
    using Clock = std::chrono::steady_clock;

    struct Spot {
        std::int32_t x = 0;
        std::int32_t y = 0;

        [[nodiscard]] bool operator==(const Spot&) const noexcept = default;
    };

    [[nodiscard]] static Spot centred_over(const Rect& window, const Rect& work);

    [[nodiscard]] std::optional<Spot> correction(void* window, bool zoomed, const Rect& rect, const Rect& work,
                                                 Clock::time_point now);
    void forget(void* window) { corrected_.erase(window); }

private:
    struct Corrected {
        Clock::time_point last;
        int times = 0;
    };

    static constexpr std::int32_t kTolerance = 1;
    static constexpr std::chrono::milliseconds kLeastBetween{100};
    static constexpr int kMostPerMaximize = 4;

    std::unordered_map<void*, Corrected> corrected_;
};

}
