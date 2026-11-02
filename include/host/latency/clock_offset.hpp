#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rwin::host {
class ClockOffset {
public:
    static constexpr std::size_t kSamples = 16;
    static constexpr std::int64_t kNanosecondsPerTick = 100;

    void take(std::int64_t sent, std::int64_t guest, std::int64_t heard) noexcept;

    [[nodiscard]] std::optional<std::int64_t> to_host(std::int64_t guest) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> round_trip() const noexcept;

private:
    struct Sample {
        std::int64_t offset = 0;
        std::int64_t round_trip = 0;
    };

    [[nodiscard]] const Sample* best() const noexcept;

    std::array<Sample, kSamples> samples_{};
    std::size_t taken_ = 0;
};

}
