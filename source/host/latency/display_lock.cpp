#include "host/latency/display_lock.hpp"

#include <algorithm>
#include <cstdlib>

namespace rwin::host {

namespace {

constexpr std::int64_t kNanosecondsTimesMillihertz = 1'000'000'000'000;
constexpr std::int64_t kLeastMillihertz = 1'000;
constexpr std::int64_t kMostMillihertz = 1'000'000;
// A refresh that moved this little is the same output's, measured again.
constexpr std::int64_t kSameRefreshNs = 2'000;
constexpr std::size_t kPercent = 100;
// Unlocked, ahead starts where nine frames in ten were.
constexpr std::size_t kStartPercent = 90;
// A step is an eighth of this output's period, within these.
constexpr std::int64_t kStepsPerPeriod = 8;
constexpr std::int64_t kLeastStepNs = 100'000;
constexpr std::int64_t kMostStepNs = 1'000'000;

}  // namespace

void DisplayLock::restart(std::int64_t refresh, std::int64_t now) noexcept {
    refresh_ = refresh;
    ahead_ = 0;
    floor_ = 0;
    taken_ = 0;
    judged_ = 0;
    late_ = 0;
    next_ = now;
    asked_.reset();
    asked_at_ = 0;
}

std::int64_t DisplayLock::step() const noexcept {
    return std::clamp(refresh_ / kStepsPerPeriod, kLeastStepNs, kMostStepNs);
}

std::int64_t DisplayLock::unlocked_took() const {
    std::array<std::int64_t, kFrames> sorted = took_;
    const std::size_t count = std::min(taken_, kFrames);
    const auto end = sorted.begin() + static_cast<std::ptrdiff_t>(count);
    const auto rank = sorted.begin() + static_cast<std::ptrdiff_t>(std::min(count - 1, count * kStartPercent / kPercent));
    std::nth_element(sorted.begin(), rank, end);
    return *rank;
}

void DisplayLock::steer() noexcept {
    if (late_ * kPercent > judged_ * kMostLatePercent) {
        ahead_ += 2 * step();
        floor_ = ahead_;
    } else if (late_ == 0 && ahead_ - step() >= floor_) {
        ahead_ -= step();
    }
    judged_ = 0;
    late_ = 0;
}

// ``presented`` is one of this output's blanks; the guest's are ahead_
// before one, a whole number of this output's periods apart.
DisplayLock::Timing DisplayLock::timing_from(std::int64_t presented) const noexcept {
    const std::int64_t periods = std::max<std::int64_t>(1, (kGuestPeriodNs + refresh_ / 2) / refresh_);
    const std::int64_t guest_period = periods * refresh_;
    const std::int64_t millihertz = std::clamp((kNanosecondsTimesMillihertz + guest_period / 2) / guest_period,
                                               kLeastMillihertz, kMostMillihertz);
    std::int64_t blank = presented - ahead_;
    while (blank <= 0) blank += guest_period;
    return Timing{static_cast<std::uint32_t>(millihertz), blank};
}

bool DisplayLock::same_phase(const Timing& timing) const noexcept {
    if (!asked_.has_value() || asked_->refresh_millihz != timing.refresh_millihz) return false;
    const std::int64_t apart = ((timing.blank - asked_->blank) % refresh_ + refresh_) % refresh_;
    return std::min(apart, refresh_ - apart) < kSamePhaseNs;
}

std::optional<DisplayLock::Timing> DisplayLock::take(const FrameStamps& frame, std::int64_t now) {
    const std::int64_t composed = frame.of(FrameStamps::Stage::Composed);
    const std::int64_t presented = frame.of(FrameStamps::Stage::Presented);
    if (composed == 0 || presented == 0 || frame.refresh_ns <= 0) return std::nullopt;
    if (std::abs(frame.refresh_ns - refresh_) > kSameRefreshNs) restart(frame.refresh_ns, now);
    const std::int64_t took = presented - composed;
    if (ahead_ == 0) {
        took_[taken_++ % kFrames] = took;
        if (now < next_ || taken_ < kFewestFrames) return std::nullopt;
        ahead_ = unlocked_took();
    } else {
        if (composed <= asked_at_) return std::nullopt;
        ++judged_;
        if (took > ahead_ + refresh_ / 2) ++late_;
        if (now < next_ || judged_ < kFewestFrames) return std::nullopt;
        steer();
    }
    next_ = now + kEveryNs;
    const Timing timing = timing_from(presented);
    if (same_phase(timing)) return std::nullopt;
    asked_ = timing;
    asked_at_ = now;
    return timing;
}

}  // namespace rwin::host
