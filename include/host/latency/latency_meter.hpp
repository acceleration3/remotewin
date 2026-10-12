#pragma once

#include "host/latency/frame_stamps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace rwin::host {

// Where the time of the frames shown went, stage by stage: each frame's
// time from the stage before (the last one it was seen at) to each stage,
// and from its first stage to its last, over the frames since the meter
// last said so.
class LatencyMeter {
public:
    // The newest this many frames are weighed; older ones are let go.
    static constexpr std::size_t kFrames = 512;

    void take(const FrameStamps& frame) noexcept;
    [[nodiscard]] std::size_t frames() const noexcept { return frames_; }

    // The median and the 95th percentile, in nanoseconds, of the time to
    // ``stage``; nothing when no frame was seen there and before it.
    struct Spread {
        std::int64_t median = 0;
        std::int64_t p95 = 0;
        std::size_t frames = 0;
    };
    [[nodiscard]] std::optional<Spread> to(FrameStamps::Stage stage) const;
    // From the first stage seen to the last.
    [[nodiscard]] std::optional<Spread> whole() const;

    // One line of it: "composed>copied 0.4/1.1 ... whole 21.3/30.2 ms
    // (median/p95)" and the refresh the compositor said.
    [[nodiscard]] std::string summary() const;
    // Weighed afresh from here.
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

}  // namespace rwin::host
