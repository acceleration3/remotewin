#pragma once

#include "host/latency/frame_stamps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace rwin::host {
class LatencyMeter {
public:
    static constexpr std::size_t kFrames = 512;

    void take(const FrameStamps& frame) noexcept;
    [[nodiscard]] std::size_t frames() const noexcept { return frames_; }

    struct Spread {
        std::int64_t median = 0;
        std::int64_t p95 = 0;
        std::size_t frames = 0;
    };
    [[nodiscard]] std::optional<Spread> to(FrameStamps::Stage stage) const;
    [[nodiscard]] std::optional<Spread> whole() const;

    [[nodiscard]] std::string summary() const;
    void restart() noexcept;

private:
    struct Durations {
        std::array<std::int64_t, kFrames> values{};
        std::size_t taken = 0;

        void add(std::int64_t value) noexcept { values[taken++ % kFrames] = value; }
        [[nodiscard]] std::optional<Spread> spread(std::array<std::int64_t, kFrames>& scratch) const;
    };

    std::array<Durations, FrameStamps::kStages> stages_{};
    Durations whole_;
    std::size_t frames_ = 0;
    std::int64_t refresh_ns_ = 0;
    mutable std::array<std::int64_t, kFrames> scratch_{};
};

}
