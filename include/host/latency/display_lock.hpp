#pragma once

#include "host/latency/frame_stamps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rwin::host {
class DisplayLock {
public:
    struct Timing {
        std::int64_t shift_ns = 0;

        [[nodiscard]] bool operator==(const Timing&) const noexcept = default;
    };

    static constexpr std::int64_t kMostRateOffPpm = 50;
    static constexpr std::size_t kFrames = 120;
    static constexpr std::size_t kFewestFrames = 60;
    static constexpr std::int64_t kEveryNs = 1'000'000'000;
    static constexpr std::size_t kFewestSweepFrames = 15;
    static constexpr std::int64_t kSweepEveryNs = 250'000'000;
    static constexpr std::int64_t kSettleNs = 3'000'000'000;
    static constexpr std::int64_t kMostGapNs = 250'000'000;
    static constexpr std::int64_t kGridMovedNs = 300'000;
    static constexpr std::int64_t kLeastBetweenSweepsNs = 60'000'000'000;
    static constexpr std::size_t kOutputs = 4;

    [[nodiscard]] std::optional<Timing> take(const FrameStamps& frame, std::int64_t now, std::uint32_t guest_microhz);

private:
    struct Output {
        void begin(std::int64_t stated_refresh) noexcept;
        void begin_sweep() noexcept;
        [[nodiscard]] std::int64_t step() const noexcept;
        [[nodiscard]] bool sweeping() const noexcept { return sweep_left != 0; }

        std::int64_t refresh = 0;
        std::int64_t latch = 0;
        std::size_t sweep_left = 0;
        int wrong_windows = 0;
        std::int64_t swept_at = 0;
        std::int64_t used_at = 0;
    };

    [[nodiscard]] Output& output_for(std::int64_t refresh, std::int64_t now) noexcept;
    [[nodiscard]] bool locks(const Output& output) const noexcept;
    void leave_window(std::int64_t composed) noexcept;
    [[nodiscard]] bool grid_kept(std::int64_t composed) const noexcept;
    [[nodiscard]] std::int64_t steer(std::int64_t now);
    [[nodiscard]] bool latch_looks_wrong(std::size_t count) const noexcept;
    [[nodiscard]] bool late_group_made_it(std::size_t count) const noexcept;
    [[nodiscard]] std::int64_t margin_short_by(std::size_t count) noexcept;

    std::array<Output, kOutputs> outputs_{};
    Output* output_ = nullptr;
    std::uint32_t guest_microhz_ = 0;
    std::int64_t guest_period_ = 0;
    std::int64_t last_presented_ = 0;
    std::int64_t last_composed_ = 0;
    std::int64_t unseen_shift_ = 0;
    std::array<std::int64_t, kFrames> commits_{};
    std::size_t judged_ = 0;
    std::int64_t next_ = 0;
    std::int64_t asked_at_ = 0;
};

}
