#include "host/latency/display_lock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rwin::host {

namespace {

constexpr std::int64_t kNanosecondsTimesMicrohertz = 1'000'000'000'000'000;
constexpr std::int64_t kMostShiftNs = 999'999'999;
constexpr std::int64_t kSameRefreshNs = 2'000;
constexpr std::size_t kRankTwentieths = 20;
constexpr std::size_t kRankQuarters = 4;
constexpr std::int64_t kGroupGapDivisor = 3;
constexpr std::size_t kLeastGroupTenths = 10;
constexpr std::size_t kLeastGroupFrames = 2;
constexpr int kWrongWindowsToSweep = 2;
constexpr std::int64_t kLatchMarginNs = 1'500'000;
constexpr std::int64_t kStepsPerPeriod = 8;
constexpr std::int64_t kLeastStepNs = 100'000;
constexpr std::int64_t kMostStepNs = 1'000'000;

}

void DisplayLock::Output::begin(std::int64_t stated_refresh) noexcept {
    *this = Output{};
    refresh = stated_refresh;
    begin_sweep();
}

void DisplayLock::Output::begin_sweep() noexcept {
    sweep_left = static_cast<std::size_t>(refresh / step()) + 1;
    latch = 0;
    wrong_windows = 0;
}

std::int64_t DisplayLock::Output::step() const noexcept {
    return std::clamp(refresh / kStepsPerPeriod, kLeastStepNs, kMostStepNs);
}

DisplayLock::Output& DisplayLock::output_for(std::int64_t refresh, std::int64_t now) noexcept {
    Output* found = nullptr;
    for (Output& output : outputs_) {
        if (output.refresh != 0 && std::abs(output.refresh - refresh) <= kSameRefreshNs) found = &output;
    }
    if (found == nullptr) {
        found = &*std::min_element(outputs_.begin(), outputs_.end(),
                                   [](const Output& a, const Output& b) { return a.used_at < b.used_at; });
        found->begin(refresh);
    }
    found->used_at = now;
    if (found != output_) {
        output_ = found;
        judged_ = 0;
        last_presented_ = 0;
        last_composed_ = 0;
        unseen_shift_ = 0;
        next_ = now;
    }
    return *found;
}

bool DisplayLock::locks(const Output& output) const noexcept {
    if (guest_period_ <= 0) return false;
    const std::int64_t periods = (guest_period_ + output.refresh / 2) / output.refresh;
    if (periods < 1) return false;
    const std::int64_t off = std::abs(guest_period_ - periods * output.refresh);
    return off * 1'000'000 <= guest_period_ * kMostRateOffPpm;
}

void DisplayLock::leave_window(std::int64_t composed) noexcept {
    judged_ = 0;
    if (output_->sweeping() && !grid_kept(composed)) output_->begin_sweep();
}

bool DisplayLock::grid_kept(std::int64_t composed) const noexcept {
    if (last_composed_ == 0) return false;
    const std::int64_t period = guest_period_;
    const std::int64_t off = ((composed + unseen_shift_ - last_composed_) % period + period) % period;
    return std::min(off, period - off) <= kGridMovedNs;
}

bool DisplayLock::late_group_made_it(std::size_t count) const noexcept {
    const std::int64_t refresh = output_->refresh;
    const std::int64_t made_it = commits_[count / kRankTwentieths];
    for (std::size_t first = count / kRankTwentieths + 1; first < count; ++first) {
        if (commits_[first] - commits_[first - 1] <= refresh / kGroupGapDivisor) continue;
        const std::size_t group = count - first;
        if (group < std::max(kLeastGroupFrames, count / kLeastGroupTenths)) return false;
        return commits_[first + group / 2] - refresh < made_it;
    }
    return false;
}

bool DisplayLock::latch_looks_wrong(std::size_t count) const noexcept {
    return commits_[count / kRankQuarters] - output_->refresh > output_->latch || late_group_made_it(count);
}

std::int64_t DisplayLock::margin_short_by(std::size_t count) noexcept {
    const std::int64_t refresh = output_->refresh;
    const std::int64_t margin = std::min(kLatchMarginNs, refresh / 2);
    for (std::size_t frame = 0; frame < count; ++frame) {
        const std::int64_t past = ((commits_[frame] - output_->latch) % refresh + refresh) % refresh;
        commits_[frame] = past > margin + refresh / 2 ? past - refresh : past;
    }
    const auto median = commits_.begin() + static_cast<std::ptrdiff_t>(count / 2);
    std::nth_element(commits_.begin(), median, commits_.begin() + static_cast<std::ptrdiff_t>(count));
    return margin - *median;
}

std::int64_t DisplayLock::steer(std::int64_t now) {
    Output& output = *output_;
    const std::size_t count = std::min(judged_, kFrames);
    judged_ = 0;
    const auto window = commits_.begin() + static_cast<std::ptrdiff_t>(count);
    std::sort(commits_.begin(), window);
    if (output.sweeping()) {
        const std::int64_t took = commits_[count / kRankQuarters];
        output.latch = output.latch == 0 ? took : std::min(output.latch, took);
        if (--output.sweep_left == 0) output.swept_at = now;
        return output.step();
    }
    if (!latch_looks_wrong(count)) {
        output.wrong_windows = 0;
    } else if (output.wrong_windows < kWrongWindowsToSweep) {
        ++output.wrong_windows;
    }
    if (output.wrong_windows > 0 && output.wrong_windows < kWrongWindowsToSweep) return 0;
    if (output.wrong_windows >= kWrongWindowsToSweep && now - output.swept_at >= kLeastBetweenSweepsNs) {
        output.begin_sweep();
        return 0;
    }
    const std::int64_t short_by = margin_short_by(count);
    const std::int64_t settled = std::min(kLatchMarginNs / 2, output.step());
    return std::abs(short_by) >= settled ? std::clamp(short_by, -kMostShiftNs, kMostShiftNs) : 0;
}

std::optional<DisplayLock::Timing> DisplayLock::take(const FrameStamps& frame, std::int64_t now,
                                                      std::uint32_t guest_microhz) {
    const std::int64_t composed = frame.of(FrameStamps::Stage::Composed);
    const std::int64_t committed = frame.of(FrameStamps::Stage::Committed);
    const std::int64_t presented = frame.of(FrameStamps::Stage::Presented);
    if (composed == 0 || committed == 0 || presented == 0 || frame.refresh_ns <= 0) return std::nullopt;
    if (guest_microhz != guest_microhz_) {
        outputs_ = {};
        output_ = nullptr;
        guest_microhz_ = guest_microhz;
        guest_period_ = guest_microhz == 0 ? 0 : kNanosecondsTimesMicrohertz / guest_microhz;
    }
    Output& output = output_for(frame.refresh_ns, now);
    const std::int64_t since_last = presented - last_presented_;
    last_presented_ = presented;
    if (since_last > kMostGapNs) leave_window(composed);
    if (!locks(output)) return std::nullopt;
    if (composed > asked_at_ && (unseen_shift_ == 0 || last_composed_ == 0 || grid_kept(composed))) {
        last_composed_ = composed;
        unseen_shift_ = 0;
    }
    const bool sweeping = output.sweeping();
    if (composed <= asked_at_ + (sweeping ? 0 : kSettleNs)) return std::nullopt;
    commits_[judged_++ % kFrames] = presented - committed;
    if (now < next_ || judged_ < (sweeping ? kFewestSweepFrames : kFewestFrames)) return std::nullopt;
    const std::int64_t shift = steer(now);
    next_ = now + (output.sweeping() ? kSweepEveryNs : kEveryNs);
    if (shift == 0) return std::nullopt;
    asked_at_ = now;
    unseen_shift_ = shift;
    return Timing{shift};
}

}
