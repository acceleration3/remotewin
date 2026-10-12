#include "host/latency/latency_meter.hpp"

#include <algorithm>
#include <format>

namespace rwin::host {

namespace {

constexpr double kMillisecondsPerNanosecond = 1e-6;
constexpr std::size_t kPercent = 100;
constexpr std::size_t kHigh = 95;

double milliseconds(std::int64_t nanoseconds) {
    return static_cast<double>(nanoseconds) * kMillisecondsPerNanosecond;
}

}  // namespace

std::optional<LatencyMeter::Spread> LatencyMeter::Durations::spread(
    std::array<std::int64_t, kFrames>& scratch) const {
    const std::size_t count = std::min(taken, kFrames);
    if (count == 0) return std::nullopt;
    std::copy_n(values.begin(), count, scratch.begin());
    const auto begin = scratch.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(count);
    const auto at = [&](std::size_t rank) {
        const auto nth = begin + static_cast<std::ptrdiff_t>(rank);
        std::nth_element(begin, nth, end);
        return *nth;
    };
    Spread spread;
    spread.frames = count;
    spread.median = at(count / 2);
    spread.p95 = at(std::min(count - 1, count * kHigh / kPercent));
    return spread;
}

// A stage a frame was not seen at is skipped over: the time to the next is
// from the one before it that was seen.
void LatencyMeter::take(const FrameStamps& frame) noexcept {
    std::int64_t first = 0;
    std::int64_t last = 0;
    for (std::size_t stage = 0; stage < FrameStamps::kStages; ++stage) {
        const std::int64_t when = frame.at[stage];
        if (when == 0) continue;
        if (last != 0) stages_[stage].add(when - last);
        if (first == 0) first = when;
        last = when;
    }
    if (first == 0) return;
    if (last != first) whole_.add(last - first);
    if (frame.refresh_ns != 0) refresh_ns_ = frame.refresh_ns;
    ++frames_;
}

std::optional<LatencyMeter::Spread> LatencyMeter::to(FrameStamps::Stage stage) const {
    return stages_[static_cast<std::size_t>(stage)].spread(scratch_);
}

std::optional<LatencyMeter::Spread> LatencyMeter::whole() const { return whole_.spread(scratch_); }

std::string LatencyMeter::summary() const {
    std::string line;
    for (std::size_t stage = 1; stage < FrameStamps::kStages; ++stage) {
        const std::optional<Spread> spread = to(static_cast<FrameStamps::Stage>(stage));
        if (!spread.has_value()) continue;
        line += std::format("{}{} {:.2f}/{:.2f}", line.empty() ? "" : ", ",
                            to_string(static_cast<FrameStamps::Stage>(stage)), milliseconds(spread->median),
                            milliseconds(spread->p95));
    }
    if (const std::optional<Spread> spread = whole()) {
        line += std::format("; whole {:.2f}/{:.2f}", milliseconds(spread->median), milliseconds(spread->p95));
    }
    line += " ms (median/p95, each from the stage before)";
    if (refresh_ns_ != 0) line += std::format("; refresh {:.2f} ms", milliseconds(refresh_ns_));
    return std::format("{} frames: {}", frames_, line);
}

void LatencyMeter::restart() noexcept {
    for (Durations& stage : stages_) stage.taken = 0;
    whole_.taken = 0;
    frames_ = 0;
}

}  // namespace rwin::host
