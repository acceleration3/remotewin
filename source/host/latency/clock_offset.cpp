#include "host/latency/clock_offset.hpp"

#include <algorithm>

namespace rwin::host {

void ClockOffset::take(std::int64_t sent, std::int64_t guest, std::int64_t heard) noexcept {
    if (heard < sent) return;
    const std::int64_t middle = sent + (heard - sent) / 2;
    samples_[taken_ % kSamples] = Sample{guest * kNanosecondsPerTick - middle, heard - sent};
    ++taken_;
}

const ClockOffset::Sample* ClockOffset::best() const noexcept {
    if (taken_ == 0) return nullptr;
    const auto end = samples_.begin() + static_cast<std::ptrdiff_t>(std::min(taken_, kSamples));
    return &*std::min_element(samples_.begin(), end,
                              [](const Sample& a, const Sample& b) { return a.round_trip < b.round_trip; });
}

std::optional<std::int64_t> ClockOffset::to_host(std::int64_t guest) const noexcept {
    const Sample* trusted = best();
    if (trusted == nullptr || guest == 0) return std::nullopt;
    return guest * kNanosecondsPerTick - trusted->offset;
}

std::optional<std::int64_t> ClockOffset::round_trip() const noexcept {
    const Sample* trusted = best();
    if (trusted == nullptr) return std::nullopt;
    return trusted->round_trip;
}

}  // namespace rwin::host
