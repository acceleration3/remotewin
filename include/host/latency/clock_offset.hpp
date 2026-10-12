#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rwin::host {

// How far the guest's clock (its performance counter, in 100 ns units) is
// from this side's (now_ns), from probes the guest answered.
//
// A probe's answer was read on the guest somewhere between the probe going
// and its answer coming back, so each one puts the offset within half its
// round trip. The answer that crossed fastest of the last few is the one
// trusted: one that waited behind a frame on the guest, or in the guest's
// network, is off by however long it waited, on one side only.
class ClockOffset {
public:
    // Kept, so a guest clock drifting from this one is followed.
    static constexpr std::size_t kSamples = 16;
    static constexpr std::int64_t kNanosecondsPerTick = 100;

    // A probe sent at ``sent``, answered with the guest's ``guest``, heard
    // back at ``heard``. An answer heard before it was sent is no answer.
    void take(std::int64_t sent, std::int64_t guest, std::int64_t heard) noexcept;

    // ``guest`` on this side's clock; nothing before the first answer, or
    // for 0, which the guest sends for a moment it did not see.
    [[nodiscard]] std::optional<std::int64_t> to_host(std::int64_t guest) const noexcept;
    // The round trip of the answer trusted: how far off to_host can be,
    // twice over.
    [[nodiscard]] std::optional<std::int64_t> round_trip() const noexcept;

private:
    struct Sample {
        // Guest minus this side, in nanoseconds.
        std::int64_t offset = 0;
        std::int64_t round_trip = 0;
    };

    [[nodiscard]] const Sample* best() const noexcept;

    std::array<Sample, kSamples> samples_{};
    std::size_t taken_ = 0;
};

}  // namespace rwin::host
