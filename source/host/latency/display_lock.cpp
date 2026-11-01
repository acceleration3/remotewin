#include "host/latency/display_lock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rwin::host {

namespace {

constexpr std::int64_t kNanosecondsTimesMicrohertz = 1'000'000'000'000'000;
// The wire takes a shift of less than a second either way.
constexpr std::int64_t kMostShiftNs = 999'999'999;
// A refresh that moved this little is the same output's, stated again.
constexpr std::int64_t kSameRefreshNs = 2'000;
// A judgement's bound of the latch rests on the frame one twentieth of the way
// up the window: below it, one frame is the wandering of a commit and not the
// latch.
constexpr std::size_t kRankTwentieths = 20;
// The frame a quarter of the way up the window is the first that a window
// nearly all late has missed the latch with.
constexpr std::size_t kRankQuarters = 4;
// Commits further apart than this fraction of a refresh, in the sorted window,
// are two groups: frames that made the latch and frames that did not.
constexpr std::int64_t kGroupGapDivisor = 3;
// The late group is a tenth of the window at the least, and two frames.
constexpr std::size_t kLeastGroupTenths = 10;
constexpr std::size_t kLeastGroupFrames = 2;
// Windows in a row the latch cannot explain, before it is swept for again.
constexpr int kWrongWindowsToSweep = 2;
// How far before the latch commits are kept: more than they wander, measured
// at about a millisecond between 5% and 95%. Any further is only latency.
constexpr std::int64_t kLatchMarginNs = 1'500'000;
// A step is an eighth of this output's period, within these.
constexpr std::int64_t kStepsPerPeriod = 8;
constexpr std::int64_t kLeastStepNs = 100'000;
constexpr std::int64_t kMostStepNs = 1'000'000;

}  // namespace

void DisplayLock::Output::begin(std::int64_t stated_refresh) noexcept {
    *this = Output{};
    refresh = stated_refresh;
    begin_sweep();
}

// From scratch: the latch is what this sweep finds.
void DisplayLock::Output::begin_sweep() noexcept {
    sweep_left = static_cast<std::size_t>(refresh / step()) + 1;
    latch = 0;
    wrong_windows = 0;
}

std::int64_t DisplayLock::Output::step() const noexcept {
    return std::clamp(refresh / kStepsPerPeriod, kLeastStepNs, kMostStepNs);
}

// The output these frames were shown on: the known one of that refresh, or
// the one least recently used, forgotten and begun. Another output than the
// frames before are of is a window of its own, and nothing of the frames
// before it says how it was measured.
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

// Whether the guest's period is the whole number, one at least, of this
// output's periods that it is nearest: the only rate at which a frame's commit
// falls at the same point of the output's period each time.
bool DisplayLock::locks(const Output& output) const noexcept {
    if (guest_period_ <= 0) return false;
    const std::int64_t periods = (guest_period_ + output.refresh / 2) / output.refresh;
    if (periods < 1) return false;
    const std::int64_t off = std::abs(guest_period_ - periods * output.refresh);
    return off * 1'000'000 <= guest_period_ * kMostRateOffPpm;
}

// Frames that were not shown in sequence. A sweep under way goes on from the
// step it reached if the guest's blanks are where it left them, so that an
// application that draws in bursts finishes it in the end; if another mirror
// moved them meanwhile, the sweep has seen phases that were not swept and
// would settle on a latch that is not the output's, and starts over.
void DisplayLock::leave_window(std::int64_t composed) noexcept {
    judged_ = 0;
    if (output_->sweeping() && !grid_kept(composed)) output_->begin_sweep();
}

// Whether a frame composed at ``composed`` is on the grid of blanks the last
// frame seen was, less the shifts asked since, modulo the guest's period: the
// blanks kept running at the rate asked while nothing was shown.
bool DisplayLock::grid_kept(std::int64_t composed) const noexcept {
    if (last_composed_ == 0) return false;
    const std::int64_t period = guest_period_;
    const std::int64_t off = ((composed + unseen_shift_ - last_composed_) % period + period) % period;
    return std::min(off, period - off) <= kGridMovedNs;
}

// Whether the sorted window has a second group, apart from the frames that
// made the latch, whose middle is before the first of those that did: a
// latch that lies between them. Frames the compositor showed a refresh late
// were committed as early as the rest, so their middle, less the refresh,
// is among those on time and is no such sign. Few frames are the wandering
// of a commit and not a group.
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

// Either nearly all of the window missed the latch known, or some missed it
// and some made it, cleanly apart. The window is sorted. Nearly all is the
// frame a quarter of the way up: a stall of the compositor lasting a second
// and a fifth is split by two windows, each of which most of it fills, but
// neither three quarters.
bool DisplayLock::latch_looks_wrong(std::size_t count) const noexcept {
    return commits_[count / kRankQuarters] - output_->refresh > output_->latch || late_group_made_it(count);
}

// By the median frame, each frame placed on whichever side of the latch is
// the nearer way round to the margin. Large means commits are too close.
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

// The window is sorted, then each frame's commit read as how long it took to
// reach the screen: a commit that just made the latch took it (L), and one
// that just missed took a refresh more (R). Only a sweep says where the latch
// is: the least any of its steps took, by the frame a quarter of the way up each
// step. Not its quickest frames, nor any frame since: a compositor sometimes catches a commit past its
// latch, and following those walks commits to where most of them miss. Too high
// a latch costs margin, too low costs frames. After the sweep, a window that
// cannot be what that latch gives does not steer, and two in a row sweep again,
// once a minute at the most. The shift that moves the guest's blanks earlier by the sweep's
// step, or until commits are the margin past the latch; 0 within half the
// margin of it.
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
    // Half the margin can be half of all there is on a fast output, which is
    // no margin once commits wander: there, a sweep's step.
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
        // The guest runs at another rate: what was learnt of its cadence is
        // of the old one.
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
    // A blank between the ask and the device's taking it is newer than the ask
    // and still on the old grid: the shift is seen only by a frame that fits it.
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

}  // namespace rwin::host
