#include "host/latency/cadence_meter.hpp"
#include "host/latency/clock_offset.hpp"
#include "host/latency/display_lock.hpp"
#include "host/latency/frame_stamps.hpp"
#include "host/latency/latency_meter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using Stage = rwin::host::FrameStamps::Stage;

constexpr std::int64_t kAhead = 5'000'000;

std::int64_t guest_at(std::int64_t host) { return (host + kAhead) / 100; }

void the_fastest_answer_is_trusted() {
    rwin::host::ClockOffset offset;
    check(!offset.to_host(guest_at(1'000'000)).has_value(), "nothing is known before an answer");
    offset.take(1'000'000, guest_at(1'100'000), 1'200'000);
    check(offset.to_host(guest_at(9'000'000)) == 9'000'000, "an answer from the middle puts the guest exactly");
    offset.take(2'000'000, guest_at(4'000'000), 4'100'000);
    check(offset.to_host(guest_at(9'000'000)) == 9'000'000, "a slow answer does not move a fast one");
    check(offset.round_trip() == 200'000, "the round trip said is the trusted answer's");
    check(!offset.to_host(0).has_value(), "the guest's 0 is a moment it did not see");
    offset.take(5'000'000, guest_at(5'000'000), 4'000'000);
    check(offset.round_trip() == 200'000, "an answer heard before it was sent is no answer");
}

void old_answers_are_let_go() {
    rwin::host::ClockOffset offset;
    offset.take(0, guest_at(50), 100);
    for (std::size_t probe = 1; probe <= rwin::host::ClockOffset::kSamples; ++probe) {
        const auto sent = static_cast<std::int64_t>(probe) * 1'000'000'000;
        offset.take(sent, guest_at(sent + 1'000'000 + 500), sent + 1'000);
    }
    check(offset.to_host(guest_at(2'000'000'000)) == 2'000'000'000 - 1'000'000,
          "the clock is followed once the old answers are gone");
}

rwin::host::FrameStamps frame(std::int64_t composed, std::int64_t received, std::int64_t drawn) {
    rwin::host::FrameStamps stamps;
    stamps.note(Stage::Composed, composed);
    stamps.note(Stage::Received, received);
    stamps.note(Stage::Acquired, drawn);
    return stamps;
}

void each_stage_is_timed_from_the_one_before() {
    rwin::host::LatencyMeter meter;
    for (std::int64_t at = 1; at <= 19; ++at) {
        meter.take(frame(at * 1'000'000, at * 1'000'000 + 3'000'000, at * 1'000'000 + 4'000'000));
    }
    meter.take(frame(100'000'000, 100'000'000 + 30'000'000, 100'000'000 + 31'000'000));
    meter.take(frame(200'000'000, 200'000'000 + 30'000'000, 200'000'000 + 31'000'000));
    check(meter.frames() == 21, "every frame is counted");
    const auto received = meter.to(Stage::Received);
    check(received.has_value() && received->median == 3'000'000, "received is timed from composed");
    check(received.has_value() && received->p95 == 30'000'000, "the slow frames are the 95th percentile");
    const auto drawn = meter.to(Stage::Acquired);
    check(drawn.has_value() && drawn->median == 1'000'000, "drawn is timed from received, the stage seen before it");
    check(!meter.to(Stage::Copied).has_value(), "a stage never seen has no spread");
    const auto whole = meter.whole();
    check(whole.has_value() && whole->median == 4'000'000, "the whole is from the first stage to the last");
    meter.restart();
    check(meter.frames() == 0 && !meter.whole().has_value(), "restarted, nothing is weighed");
}

void a_frame_seen_nowhere_is_not_counted() {
    rwin::host::LatencyMeter meter;
    meter.take(rwin::host::FrameStamps{});
    check(meter.frames() == 0, "a frame with no stamp is no frame");
}

struct Output {
    std::int64_t stated = 16'666'667;
    std::int64_t real = 16'666'667;
    std::int64_t phase = 1'000'000;
    std::int64_t latch = 3'000'000;

    [[nodiscard]] std::int64_t blank_for(std::int64_t commit) const {
        const std::int64_t earliest = commit + latch - phase;
        return (earliest + real - 1) / real * real + phase;
    }
    [[nodiscard]] std::int64_t margin_of(std::int64_t commit) const {
        const std::int64_t slack = blank_for(commit) - commit - latch;
        return slack > real / 2 ? slack - real : slack;
    }
};

std::uint32_t rate_on(const Output& output) {
    constexpr std::int64_t kNearest180HzNs = 5'555'556;
    const std::int64_t periods = std::max<std::int64_t>(1, (kNearest180HzNs + output.stated / 2) / output.stated);
    const double exact = 1e15 / static_cast<double>(periods * output.stated);
    return static_cast<std::uint32_t>(std::llround(exact / 1000.0) * 1000);
}

constexpr std::int64_t kMargin = 1'500'000;
constexpr std::int64_t kDeadband = kMargin / 2;
constexpr std::int64_t kLatchError = 1'000'000;

class Blanks {
public:
    Blanks(std::int64_t start, std::int64_t microhz) : microhz_(microhz), epoch_(start), last_(start) {}

    [[nodiscard]] std::int64_t next() const { return due(count_ + 1); }
    void pass() { last_ = due(++count_); }
    void align(std::uint32_t microhz, std::int64_t shift, std::int64_t now) {
        std::int64_t next = this->next() - shift;
        microhz_ = microhz;
        const std::int64_t period = kNanosecondsTimesMicrohertz / microhz_;
        const std::int64_t earliest = std::max(last_ + period / 2, now + 1);
        if (next < earliest) next += (earliest - next + period - 1) / period * period;
        epoch_ = next - period;
        count_ = 0;
    }
    [[nodiscard]] std::int64_t microhz() const { return microhz_; }

private:
    static constexpr std::int64_t kNanosecondsTimesMicrohertz = 1'000'000'000'000'000;

    [[nodiscard]] std::int64_t due(std::int64_t count) const {
        return epoch_ + count * (kNanosecondsTimesMicrohertz / microhz_)
               + count * (kNanosecondsTimesMicrohertz % microhz_) / microhz_;
    }

    std::int64_t microhz_ = 60'000'000;
    std::int64_t epoch_;
    std::int64_t count_ = 0;
    std::int64_t last_;
};

class Guest {
public:
    Guest(rwin::host::DisplayLock& lock, Output output, std::uint32_t microhz = 0)
        : lock_(lock), output_(output), blanks_(kStart, microhz != 0 ? microhz : rate_on(output)) {}

    void set_pipeline(std::int64_t pipeline) { pipeline_ = pipeline; }
    void set_wander(std::int64_t wander) { wander_ = wander; }
    void set_latch(std::int64_t latch) { output_.latch = latch; }
    void set_output(Output output) {
        output_ = output;
        in_flight_.clear();
    }
    void set_compositor_misses(double percent) { miss_percent_ = percent; }
    void set_lucky_frames(double percent) { lucky_percent_ = percent; }
    void set_composed_jitter(std::int64_t jitter) { composed_jitter_ = jitter; }
    void set_all_late(bool late) { all_late_ = late; }
    void set_gaussian_wander(std::int64_t width) { sigma_ = static_cast<double>(width) / kP5ToP95Sigmas; }
    void set_rate(std::uint32_t microhz) { blanks_.align(microhz, 0, now_); }

    void move_phase(std::int64_t shift) {
        blanks_.align(static_cast<std::uint32_t>(blanks_.microhz()), shift, now_);
    }

    struct Asked {
        rwin::host::DisplayLock::Timing timing;
        std::int64_t sweep_step = 0;
    };
    [[nodiscard]] const std::vector<Asked>& asked() const { return asked_; }
    [[nodiscard]] int sweeps() const {
        int sweeps = 0;
        std::size_t run = 0;
        for (const Asked& ask : asked_) {
            run = ask.timing.shift_ns == ask.sweep_step ? run + 1 : 0;
            sweeps += run == kSweepRun ? 1 : 0;
        }
        return sweeps;
    }
    [[nodiscard]] int sweep_steps() const {
        return static_cast<int>(std::count_if(asked_.begin(), asked_.end(),
                                              [](const Asked& ask) { return ask.timing.shift_ns == ask.sweep_step; }));
    }
    [[nodiscard]] double crossed() const {
        return frames_ == 0 ? 0.0 : static_cast<double>(crossed_) / static_cast<double>(frames_);
    }
    void count_crossings_from_here() { frames_ = crossed_ = 0; }

    int run(std::int64_t duration) {
        const std::int64_t end = now_ + duration;
        int timings = 0;
        Event event;
        while ((event = step(end)).happened) timings += event.asked ? 1 : 0;
        now_ = end;
        return timings;
    }

    bool run_to_timing(std::int64_t most) {
        const std::int64_t end = now_ + most;
        Event event;
        while ((event = step(end)).happened) {
            if (event.asked) return true;
        }
        now_ = end;
        return false;
    }

    void pause(std::int64_t duration) {
        silent_ = true;
        static_cast<void>(run(duration));
        silent_ = false;
    }

    [[nodiscard]] std::int64_t margin(std::size_t frames = 60) const {
        std::vector<std::int64_t> margins(margins_.end() - static_cast<std::ptrdiff_t>(std::min(frames, margins_.size())),
                                          margins_.end());
        std::nth_element(margins.begin(), margins.begin() + static_cast<std::ptrdiff_t>(margins.size() / 2),
                         margins.end());
        return margins[margins.size() / 2];
    }
    [[nodiscard]] int misses(std::size_t frames) const {
        return static_cast<int>(std::count_if(margins_.end() - static_cast<std::ptrdiff_t>(std::min(frames, margins_.size())),
                                              margins_.end(), [](std::int64_t margin) { return margin < 0; }));
    }
    [[nodiscard]] std::uint32_t microhz() const { return static_cast<std::uint32_t>(blanks_.microhz()); }

private:
    static constexpr std::int64_t kStart = 1'000'000'000;
    static constexpr double kP5ToP95Sigmas = 3.289707;
    static constexpr std::size_t kSweepRun = 8;

    struct Event {
        bool happened = false;
        bool asked = false;
    };

    Event step(std::int64_t end) {
        const std::int64_t blank = blanks_.next();
        const std::int64_t shown = in_flight_.empty() ? INT64_MAX : in_flight_.begin()->first;
        if (std::min(blank, shown) > end) return {};
        if (blank <= shown) {
            now_ = blank;
            blanks_.pass();
            compose(blank);
            return {true, false};
        }
        now_ = shown;
        const bool asked = show(in_flight_.begin()->second);
        in_flight_.erase(in_flight_.begin());
        return {true, asked};
    }

    double uniform() {
        random_ = random_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return (static_cast<double>(random_ >> 11) + 0.5) / 9007199254740992.0;
    }
    std::int64_t wander() {
        if (sigma_ != 0.0) {
            const double radius = std::sqrt(-2.0 * std::log(uniform()));
            return static_cast<std::int64_t>(sigma_ * radius * std::cos(2.0 * 3.14159265358979323846 * uniform()));
        }
        if (wander_ == 0) return 0;
        random_ = random_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::int64_t>((random_ >> 33) % static_cast<std::uint64_t>(2 * wander_ + 1)) - wander_;
    }
    bool compositor_misses() {
        if (all_late_) return true;
        if (miss_percent_ == 0.0) return false;
        return uniform() * 100.0 < miss_percent_;
    }

    void compose(std::int64_t blank) {
        if (silent_) return;
        const std::int64_t committed = blank + pipeline_ + wander();
        const std::int64_t light = output_.blank_for(committed) + (compositor_misses() ? output_.real : 0);
        const bool lucky = lucky_percent_ != 0.0 && uniform() * 100.0 < lucky_percent_;
        rwin::host::FrameStamps stamps;
        stamps.note(Stage::Composed, blank + (composed_jitter_ == 0 ? 0 : static_cast<std::int64_t>((uniform() * 2.0 - 1.0) * static_cast<double>(composed_jitter_))));
        stamps.note(Stage::Committed, lucky ? light - 1'000'000 : committed);
        stamps.note(Stage::Presented, light);
        stamps.refresh_ns = output_.stated;
        margins_.push_back(output_.margin_of(committed));
        ++frames_;
        crossed_ += output_.blank_for(committed) != output_.blank_for(blank + pipeline_) ? 1 : 0;
        if (margins_.size() > kKept) margins_.erase(margins_.begin(), margins_.begin() + kKept / 2);
        in_flight_.emplace(stamps.of(Stage::Presented), stamps);
    }

    bool show(const rwin::host::FrameStamps& stamps) {
        const auto timing = lock_.take(stamps, now_, microhz());
        if (!timing.has_value()) return false;
        asked_.push_back({*timing, std::clamp<std::int64_t>(output_.stated / 8, 100'000, 1'000'000)});
        blanks_.align(microhz(), timing->shift_ns, now_);
        return true;
    }

    static constexpr std::size_t kKept = 4000;

    rwin::host::DisplayLock& lock_;
    Output output_;
    Blanks blanks_;
    std::int64_t now_ = kStart;
    std::int64_t pipeline_ = 4'000'000;
    std::int64_t wander_ = 0;
    double lucky_percent_ = 0.0;
    std::int64_t composed_jitter_ = 0;
    double miss_percent_ = 0.0;
    double sigma_ = 0.0;
    bool all_late_ = false;
    bool silent_ = false;
    std::uint64_t frames_ = 0;
    std::uint64_t crossed_ = 0;
    std::vector<Asked> asked_;
    std::uint64_t random_ = 12345;
    std::multimap<std::int64_t, rwin::host::FrameStamps> in_flight_;
    std::vector<std::int64_t> margins_;
};

constexpr std::int64_t kSecond = 1'000'000'000;
constexpr std::int64_t kFrame = 16'666'667;

bool margin_is_kept(const Guest& guest, std::int64_t margin = kMargin, std::int64_t period = kFrame) {
    const std::int64_t off = ((guest.margin() - margin) % period + period + period / 2) % period - period / 2;
    return std::abs(off) <= kDeadband + kLatchError;
}

constexpr std::int64_t kWindowBreak = rwin::host::DisplayLock::kMostGapNs + 50'000'000;

void commits_are_kept_a_margin_before_the_latch_and_left_there() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    check(guest.run(14 * kFrame) == 0, "no timing before enough frames were shown");
    check(guest.run(3 * kFrame) >= 1, "a timing is asked for soon after enough frames");
    static_cast<void>(guest.run(20 * kSecond));
    check(margin_is_kept(guest), "commits end a margin before the latch");
    check(guest.misses(600) == 0, "and none of them misses it");
    check(guest.run(60 * kSecond) == 0, "settled, nothing is asked for in a minute");
    check(margin_is_kept(guest), "and they are still there");
}

void a_sweep_is_over_in_seconds() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    static_cast<void>(guest.run(14 * kSecond));
    check(guest.run(10 * kSecond) == 0 && margin_is_kept(guest),
          "fourteen seconds settle on a 60 Hz output, the longest sweep");
}

void an_output_off_the_millihertz_grid_is_held_for_half_an_hour(std::int64_t real) {
    rwin::host::DisplayLock lock;
    Output output;
    output.real = real;
    Guest guest{lock, output};
    guest.set_wander(300'000);
    static_cast<void>(guest.run(30 * kSecond));
    int timings = 0;
    bool kept = true;
    for (int tenth_minute = 0; tenth_minute < 180; ++tenth_minute) {
        timings += guest.run(10 * kSecond);
        kept = kept && margin_is_kept(guest);
    }
    check(timings <= 24, "half an hour on a period off the millihertz grid asks for a timing every ninety seconds at most");
    check(kept, "and commits stay within the deadband of the margin throughout");
    check(guest.misses(600) == 0, "none missing the latch");
}

void a_slower_guest_is_moved_back_in_one_correction() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    static_cast<void>(guest.run(30 * kSecond));
    guest.pause(kWindowBreak);
    guest.set_pipeline(6'000'000);
    const int timings = guest.run(30 * kSecond);
    check(timings == 1, "a guest slower by 2 ms is put back in one timing");
    check(margin_is_kept(guest), "and commits are the margin before the latch again");
}

void a_latch_that_moves_and_stays_is_swept_for_once() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    static_cast<void>(guest.run(30 * kSecond));
    const int swept = guest.sweeps();
    guest.pause(kWindowBreak);
    guest.set_latch(Output{}.latch + 4'000'000);
    static_cast<void>(guest.run(kSecond / 2));
    check(guest.misses(20) == 20, "every commit misses a latch 4 ms later");
    static_cast<void>(guest.run(150 * kSecond));
    check(guest.sweeps() - swept == 1, "and the latch is swept for again, once");
    check(guest.misses(600) == 0 && margin_is_kept(guest), "commits end the margin before the new latch");
}

void a_latch_that_moves_through_wandering_commits_is_swept_for_once() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_wander(1'000'000);
    static_cast<void>(guest.run(30 * kSecond));
    check(guest.misses(600) == 0, "wandering by a millisecond, commits settled clear of the latch");
    const int swept = guest.sweeps();
    guest.pause(kWindowBreak);
    guest.set_latch(Output{}.latch + 1'100'000);
    static_cast<void>(guest.run(kSecond / 2));
    const int straddling = guest.misses(24);
    check(straddling >= 3 && straddling <= 14, "about a third of commits straddle the latch that moved");
    static_cast<void>(guest.run(150 * kSecond));
    check(guest.sweeps() - swept == 1, "the latch is swept for again, once");
    check(guest.misses(600) == 0, "and the misses stop");
}

void a_compositor_that_misses_blanks_of_its_own_changes_nothing(int percent, std::int64_t wander) {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_wander(wander);
    static_cast<void>(guest.run(30 * kSecond));
    const std::int64_t margin = guest.margin(120);
    guest.set_compositor_misses(percent);
    const int timings = guest.run(60 * kSecond);
    check(timings == 0, "frames a refresh late through the compositor's fault ask for nothing");
    check(std::abs(guest.margin(600) - margin) <= (wander == 0 ? 5'000 : 100'000), "and the phase is where it was");
}

void a_pause_of_an_hour_leaves_the_frames_before_it_behind() {
    rwin::host::DisplayLock lock;
    Output output;
    output.real = 16'666'817;
    Guest guest{lock, output};
    while (guest.run(kFrame) == 0) {
    }
    static_cast<void>(guest.run(10 * kFrame));
    guest.pause(3600 * kSecond);
    check(guest.run(12 * kFrame) == 0, "frames after the pause are not judged with those before it");
    static_cast<void>(guest.run(60 * kSecond));
    check(guest.microhz() == rate_on(output), "the rate is the device's still");
    check(margin_is_kept(guest) && guest.misses(300) == 0, "and commits are the margin before the latch");
}

void another_mirror_moving_the_phase_is_undone_once_if_it_matters() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    static_cast<void>(guest.run(30 * kSecond));
    const std::int64_t before = guest.margin();
    guest.pause(5 * kSecond);
    guest.move_phase(4'000'000);
    guest.pause(5 * kSecond);
    static_cast<void>(guest.run(4 * kFrame));
    check(std::abs(guest.margin(3) - before - 4'000'000) <= 1'000, "another mirror moved the guest's phase by 4 ms");
    check(guest.run(30 * kSecond) == 1, "one correction on return");
    check(margin_is_kept(guest), "puts it back");

    rwin::host::DisplayLock other_lock;
    Guest other{other_lock, Output{}};
    static_cast<void>(other.run(30 * kSecond));
    const std::int64_t margin = other.margin();
    other.pause(5 * kSecond);
    other.move_phase(400'000);
    other.pause(5 * kSecond);
    const std::size_t asked = other.asked().size();
    check(other.run(30 * kSecond) == 0 && other.asked().size() == asked, "inside the deadband nothing is asked on return");
    check(std::abs(other.margin() - margin - 400'000) <= 1'000, "and the phase stays where the other mirror put it");
}

void a_transient_while_the_guest_settles_asks_for_nothing_more() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    static_cast<void>(guest.run(30 * kSecond));
    guest.pause(kWindowBreak);
    guest.set_pipeline(6'000'000);
    check(guest.run_to_timing(10 * kSecond), "a guest slower by 2 ms is timed");
    guest.set_pipeline(10'000'000);
    check(guest.run(2 * kSecond) == 0, "nothing is asked for in the transient");
    guest.set_pipeline(6'000'000);
    check(guest.run(30 * kSecond) == 0, "nor once it is over, with the guest where the timing put it");
    check(margin_is_kept(guest), "the margin the timing asked for");
}

void fast_outputs_settle_on_the_whole_number_of_their_periods_nearest_the_guest_s() {
    {
        rwin::host::DisplayLock lock;
        Output output;
        output.stated = output.real = 13'338'135;
        Guest guest{lock, output};
        static_cast<void>(guest.run(40 * kSecond));
        check(guest.run(20 * kSecond) == 0, "a 75 Hz output settles");
        check(std::abs(static_cast<double>(guest.microhz()) - 1e15 / 13'338'135.0) <= 1000.0, "at one of its periods a blank");
        check(margin_is_kept(guest) && guest.misses(300) == 0, "commits a margin before the latch");
    }
    {
        rwin::host::DisplayLock lock;
        Output output;
        output.stated = output.real = 2'777'964;
        Guest guest{lock, output};
        static_cast<void>(guest.run(40 * kSecond));
        check(guest.run(20 * kSecond) == 0, "a 360 Hz output settles");
        check(std::abs(static_cast<double>(guest.microhz()) - 1e15 / (2.0 * 2'777'964.0)) <= 1000.0, "at two of its periods a blank");
        check(margin_is_kept(guest, output.real / 2, output.real),
              "commits half a period before the latch, which is as far as a margin goes");
    }
}

void a_change_of_stated_refresh_is_swept_again() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}, 60'000'000};
    static_cast<void>(guest.run(30 * kSecond));
    check(guest.run(20 * kSecond) == 0, "settled on a 60 Hz output");
    Output moved;
    moved.stated = moved.real = 8'333'333;
    guest.set_output(moved);
    const int sweep = guest.run(10 * kSecond);
    check(sweep >= 8, "the window moved to a 120 Hz output is swept a step at a time");
    check(guest.run(20 * kSecond) == 0 && margin_is_kept(guest), "and settles");
}

constexpr std::int64_t kSecondsPerMinute = 60;

Output output_at(double hz, std::int64_t phase = 1'000'000) {
    Output output;
    output.stated = output.real = static_cast<std::int64_t>(std::llround(1e9 / hz));
    output.phase = phase;
    return output;
}

void compositor_misses_for_half_an_hour_ask_for_nothing(double percent) {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_wander(300'000);
    guest.set_compositor_misses(percent);
    static_cast<void>(guest.run(60 * kSecond));
    int timings = 0;
    std::int64_t widest = 0;
    for (int tenth_minute = 0; tenth_minute < 180; ++tenth_minute) {
        timings += guest.run(10 * kSecond);
        widest = std::max(widest, guest.margin(120));
    }
    check(timings <= 1, "half an hour of the compositor's own misses asks for one timing at most");
    check(widest <= 2'500'000, "and the margin is never above 2.5 ms");
}

void frames_the_compositor_catches_late_move_nothing(double percent) {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_wander(300'000);
    guest.set_lucky_frames(percent);
    static_cast<void>(guest.run(40 * kSecond));
    const std::int64_t margin = guest.margin(120);
    check(guest.run(600 * kSecond) == 0, "ten minutes of frames caught late ask for nothing");
    check(std::abs(guest.margin(120) - margin) <= 400'000, "and commits stay where they were");
    check(margin >= 1'000'000, "a margin clear of the latch");
}

void bursts_of_late_frames_move_nothing(double hz) {
    rwin::host::DisplayLock lock;
    Guest guest{lock, output_at(hz)};
    guest.set_wander(300'000);
    static_cast<void>(guest.run(40 * kSecond));
    const std::int64_t margin = guest.margin(120);
    const int swept = guest.sweeps();
    int timings = 0;
    for (int burst = 0; burst < 10; ++burst) {
        timings += guest.run(28 * kSecond + 800'000'000);
        guest.set_all_late(true);
        timings += guest.run(1'200'000'000);
        guest.set_all_late(false);
    }
    timings += guest.run(30 * kSecond);
    check(timings == 0 && guest.sweeps() == swept, "ten bursts of late frames ask for nothing");
    check(std::abs(guest.margin(120) - margin) <= 100'000, "and the latch is where it was");
}

void wandering_commits_at_360_hertz_stay_clear_of_the_latch() {
    struct Spread {
        std::int64_t width;
        double most_crossed;
    };
    for (const Spread spread : {Spread{500'000, 0.01}, Spread{1'000'000, 0.01}, Spread{1'500'000, 0.02}}) {
        rwin::host::DisplayLock lock;
        Guest guest{lock, output_at(1e9 / 2'777'964.0, 500'000)};
        guest.set_gaussian_wander(spread.width);
        static_cast<void>(guest.run(40 * kSecond));
        guest.count_crossings_from_here();
        const int timings = guest.run(10 * kSecondsPerMinute * kSecond);
        check(timings <= 2, "ten minutes at 360 Hz ask for two timings at most");
        check(guest.crossed() < spread.most_crossed, "and few commits cross the latch");
    }
}

void commits_wandering_over_the_latch_now_and_then_are_not_the_latch_moving(std::int64_t width) {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_gaussian_wander(width);
    static_cast<void>(guest.run(40 * kSecond));
    const int swept = guest.sweeps();
    guest.count_crossings_from_here();
    const int timings = guest.run(10 * kSecondsPerMinute * kSecond);
    check(timings <= 1 && guest.sweeps() == swept, "ten minutes of wandering over the latch now and then sweep for nothing");
}

void sustained_overload_sweeps_once_a_minute_at_most() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_wander(300'000);
    static_cast<void>(guest.run(40 * kSecond));
    guest.set_compositor_misses(80);
    static_cast<void>(guest.run(10 * kSecondsPerMinute * kSecond));
    check(guest.sweeps() <= 10, "eighty percent of frames late for ten minutes sweeps ten times at most");
}

void a_latch_that_keeps_moving_is_swept_for_once_a_minute_at_most() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_wander(300'000);
    static_cast<void>(guest.run(40 * kSecond));
    for (int move = 0; move < 30; ++move) {
        guest.set_latch(Output{}.latch + (move % 2 == 0 ? 4'000'000 : 0));
        static_cast<void>(guest.run(20 * kSecond));
    }
    check(guest.sweeps() <= 11, "a latch moved thirty times in ten minutes is swept for ten times at most");
}

void two_refresh_rates_in_one_process_are_swept_once_each() {
    rwin::host::DisplayLock lock;
    const Output outputs[2] = {output_at(60.0), output_at(120.0)};
    Guest guest{lock, outputs[0], 60'000'000};
    guest.set_wander(300'000);
    for (int first_visit = 0; first_visit < 2; ++first_visit) {
        guest.set_output(outputs[first_visit]);
        static_cast<void>(guest.run(20 * kSecond));
    }
    check(guest.sweeps() == 2, "each output is swept on its first visit");
    const int swept = guest.sweeps();
    int unsettled = 0;
    for (int visit = 0; visit < 60; ++visit) {
        const Output& output = outputs[visit % 2];
        guest.set_output(output);
        static_cast<void>(guest.run(12 * kSecond));
        unsettled += margin_is_kept(guest, std::min(kMargin, output.real / 2), output.real) ? 0 : 1;
    }
    check(guest.sweeps() == swept, "sixty returns sweep nothing");
    check(guest.microhz() == 60'000'000, "the guest is never given another rate");
    check(unsettled == 0, "and ends with commits the margin before its latch");
}

void a_guest_that_is_not_a_multiple_of_the_output_is_never_timed() {
    struct Case {
        double output_hz;
        std::uint32_t guest_microhz;
    };
    for (const Case item : {Case{60.0, 144'000'000}, Case{240.0, 144'000'000}, Case{59.94, 60'000'000},
                            Case{360.0, 100'000'000}}) {
        rwin::host::DisplayLock lock;
        Guest guest{lock, output_at(item.output_hz), item.guest_microhz};
        guest.set_wander(300'000);
        check(guest.run(120 * kSecond) == 0 && guest.sweep_steps() == 0, "a guest off the output's periods is not timed");
    }
    for (const Case item : {Case{60.0, 60'000'000}, Case{144.0, 144'000'000}, Case{360.0, 180'000'000},
                            Case{360.0, 360'000'000}}) {
        rwin::host::DisplayLock lock;
        Guest guest{lock, output_at(item.output_hz), item.guest_microhz};
        guest.set_wander(300'000);
        static_cast<void>(guest.run(60 * kSecond));
        check(guest.sweep_steps() >= 8, "a guest on a multiple of the output's periods is timed");
    }
}

void a_guest_that_changes_its_rate_is_started_over() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, output_at(120.0), 60'000'000};
    static_cast<void>(guest.run(30 * kSecond));
    const int steps = guest.sweep_steps();
    check(steps >= 8, "swept at 60 Hz");
    guest.set_rate(120'000'000);
    static_cast<void>(guest.run(40 * kSecond));
    check(guest.sweep_steps() - steps >= 8, "and swept again at 120 Hz");
    check(margin_is_kept(guest, std::min(kMargin, output_at(120.0).real / 2), output_at(120.0).real),
          "ending with commits the margin before the latch");
}

void a_blank_before_the_device_took_a_timing_does_not_restart_a_sweep() {
    rwin::host::DisplayLock lock;
    constexpr std::int64_t kBlank = 16'666'667;
    constexpr std::int64_t kTook = 16'500'000;
    constexpr std::int64_t kSweepStep = 1'000'000;
    const auto feed = [&](std::int64_t composed) {
        rwin::host::FrameStamps stamps;
        stamps.note(Stage::Composed, composed);
        stamps.note(Stage::Committed, composed + 4'000'000);
        const std::int64_t light = (composed + kTook + kBlank - 1) / kBlank * kBlank;
        stamps.note(Stage::Presented, light);
        stamps.refresh_ns = kBlank;
        return lock.take(stamps, light, 60'000'000);
    };
    std::int64_t composed = 10'000'000'000;
    int steps = 0;
    for (int frame = 0; frame < 60 * 60; ++frame) {
        const auto timing = feed(composed);
        if (timing.has_value() && timing->shift_ns == kSweepStep) {
            ++steps;
            if (steps == 3) {
                composed += kBlank;
                static_cast<void>(feed(composed));
                composed += 20 * kBlank - timing->shift_ns;
                continue;
            }
            composed -= timing->shift_ns;
        } else if (timing.has_value()) {
            composed -= timing->shift_ns;
        }
        composed += kBlank;
    }
    check(steps == 17, "the sweep carries on from where it was");
}

void an_interrupted_sweep_starts_over() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    static_cast<void>(guest.run(2 * kSecond));
    const int seen = guest.sweep_steps();
    check(seen >= 3 && seen <= 8, "two seconds are part of a sweep");
    guest.pause(kWindowBreak);
    guest.move_phase(7'000'000);
    static_cast<void>(guest.run(40 * kSecond));
    check(guest.sweep_steps() - seen >= 17, "after the break a whole sweep is made");
    check(margin_is_kept(guest) && guest.misses(300) == 0, "and commits end the margin before the latch");
}

void draw_in_bursts(Guest& guest, std::int64_t on, std::int64_t off, std::int64_t active) {
    for (std::int64_t drawn = 0; drawn < active; drawn += on) {
        static_cast<void>(guest.run(on));
        guest.pause(off);
    }
}

void a_sweep_is_finished_across_bursts_of_frames() {
    struct Pattern {
        std::int64_t on;
        std::int64_t off;
    };
    for (const Pattern pattern : {Pattern{kSecond, kSecond}, Pattern{3 * kSecond, kSecond}}) {
        rwin::host::DisplayLock lock;
        Guest guest{lock, Output{}};
        guest.set_wander(300'000);
        guest.set_composed_jitter(60'000);
        draw_in_bursts(guest, pattern.on, pattern.off, 40 * kSecond);
        check(guest.sweep_steps() == 17, "a whole sweep is made in forty seconds of frames, in bursts");
        const int steps = guest.sweep_steps();
        const std::size_t asked = guest.asked().size();
        draw_in_bursts(guest, pattern.on, pattern.off, 5 * kSecondsPerMinute * kSecond);
        check(guest.sweep_steps() == steps, "and no step of one is made again");
        const auto moves = std::count_if(guest.asked().begin() + static_cast<std::ptrdiff_t>(asked), guest.asked().end(),
                                         [](const Guest::Asked& ask) { return ask.timing.shift_ns != 0; });
        check(moves <= 2, "nor is more than a correction of the phase asked for");
        guest.pause(kWindowBreak);
        static_cast<void>(guest.run(60 * kSecond));
        check(margin_is_kept(guest) && guest.misses(300) == 0, "commits are the margin before the latch");
    }
}

void a_composed_time_that_wanders_does_not_restart_a_sweep() {
    rwin::host::DisplayLock lock;
    Guest guest{lock, Output{}};
    guest.set_composed_jitter(60'000);
    static_cast<void>(guest.run(kSecond));
    for (int pause = 0; pause < 12; ++pause) {
        guest.pause(kWindowBreak);
        static_cast<void>(guest.run(kSecond));
    }
    static_cast<void>(guest.run(30 * kSecond));
    check(guest.sweep_steps() == 17, "twelve pauses in a sweep, and it is made once");
}

rwin::host::FrameStamps shown(std::int64_t composed, std::int64_t pipeline) {
    Output output;
    rwin::host::FrameStamps stamps;
    stamps.note(Stage::Composed, composed);
    stamps.note(Stage::Committed, composed + pipeline);
    stamps.note(Stage::Presented, output.blank_for(composed + pipeline));
    stamps.refresh_ns = output.stated;
    return stamps;
}

void frames_without_their_stamps_say_nothing() {
    for (const Stage missing : {Stage::Presented, Stage::Committed, Stage::Composed}) {
        rwin::host::DisplayLock lock;
        rwin::host::FrameStamps incomplete = shown(1'000'000'000, 4'000'000);
        incomplete.note(missing, 0);
        for (int frame = 0; frame < 200; ++frame) {
            check(!lock.take(incomplete, 1'000'000'000 + frame, 60'000'000).has_value(), "a frame missing a stage times nothing");
        }
    }
    rwin::host::DisplayLock lock;
    rwin::host::FrameStamps unrefreshed = shown(1'000'000'000, 4'000'000);
    unrefreshed.refresh_ns = 0;
    check(!lock.take(unrefreshed, 2'000'000'000, 60'000'000).has_value(), "an output with no fixed refresh times nothing");
}

constexpr std::int64_t kRefresh = 13'333'333;

rwin::host::FrameStamps at(std::int64_t composed_blank, std::int64_t presented_refresh) {
    rwin::host::FrameStamps frame;
    frame.note(Stage::Composed, 1'000'000'000 + composed_blank * kRefresh);
    frame.note(Stage::Presented, 1'010'000'000 + presented_refresh * kRefresh + 250'000);
    frame.refresh_ns = kRefresh;
    return frame;
}

void frames_shown_in_the_rhythm_composed_are_steady() {
    rwin::host::CadenceMeter cadence;
    for (int frame = 0; frame < 10; ++frame) cadence.take(at(frame, frame));
    check(cadence.steady() == 9 && cadence.held() == 0 && cadence.rushed() == 0, "one blank, one refresh: steady");
    cadence.take(at(11, 11));
    check(cadence.steady() == 10 && cadence.unshown_blanks() == 1, "a blank with no frame is counted, and steady");
    check(cadence.longest_on_screen_ns() > 2 * kRefresh - 1'000'000, "the longest frame on screen is two refreshes");
}

void a_frame_late_to_the_screen_is_held_and_the_next_rushed() {
    rwin::host::CadenceMeter cadence;
    cadence.take(at(0, 0));
    cadence.take(at(1, 1));
    cadence.take(at(2, 3));
    cadence.take(at(3, 4));
    check(cadence.held() == 1 && cadence.rushed() == 0 && cadence.steady() == 2, "one refresh late is held once");
    cadence.take(at(4, 4));
    check(cadence.rushed() == 1, "two frames in one refresh: the one before is never seen");
    check(cadence.unshown_blanks() == 0, "a frame late to the screen leaves no blank without a frame");
}

void a_screen_at_rest_and_frames_without_their_stamps_say_nothing() {
    rwin::host::CadenceMeter cadence;
    cadence.take(at(0, 0));
    cadence.take(at(100, 100));
    check(cadence.steady() + cadence.held() + cadence.rushed() == 0, "a screen at rest is no rhythm");
    rwin::host::FrameStamps unpresented = at(101, 101);
    unpresented.note(Stage::Presented, 0);
    cadence.take(unpresented);
    cadence.take(at(102, 102));
    check(cadence.steady() == 1 && cadence.unshown_blanks() == 1, "a frame never presented leaves its blank unshown");
    cadence.take(at(103, 104));
    check(cadence.held() == 1, "the rhythm picks up from the frame seen last");
    cadence.restart();
    check(cadence.held() == 0 && cadence.longest_on_screen_ns() == 0, "restarted, nothing is counted");
    cadence.take(at(104, 105));
    check(cadence.steady() == 1, "the frame before a restart is still the one before");
}

}

int main() {
    frames_shown_in_the_rhythm_composed_are_steady();
    a_frame_late_to_the_screen_is_held_and_the_next_rushed();
    a_screen_at_rest_and_frames_without_their_stamps_say_nothing();
    the_fastest_answer_is_trusted();
    old_answers_are_let_go();
    each_stage_is_timed_from_the_one_before();
    a_frame_seen_nowhere_is_not_counted();
    commits_are_kept_a_margin_before_the_latch_and_left_there();
    a_sweep_is_over_in_seconds();
    an_output_off_the_millihertz_grid_is_held_for_half_an_hour(16'666'817);
    an_output_off_the_millihertz_grid_is_held_for_half_an_hour(16'666'517);
    a_slower_guest_is_moved_back_in_one_correction();
    a_latch_that_moves_and_stays_is_swept_for_once();
    a_latch_that_moves_through_wandering_commits_is_swept_for_once();
    a_compositor_that_misses_blanks_of_its_own_changes_nothing(5, 0);
    a_compositor_that_misses_blanks_of_its_own_changes_nothing(15, 0);
    a_compositor_that_misses_blanks_of_its_own_changes_nothing(5, 300'000);
    a_compositor_that_misses_blanks_of_its_own_changes_nothing(15, 300'000);
    a_pause_of_an_hour_leaves_the_frames_before_it_behind();
    another_mirror_moving_the_phase_is_undone_once_if_it_matters();
    a_transient_while_the_guest_settles_asks_for_nothing_more();
    fast_outputs_settle_on_the_whole_number_of_their_periods_nearest_the_guest_s();
    a_change_of_stated_refresh_is_swept_again();
    for (const double percent : {0.1, 1.0, 2.0, 5.0, 15.0}) {
        compositor_misses_for_half_an_hour_ask_for_nothing(percent);
    }
    for (const double percent : {1.0, 3.0, 6.0}) frames_the_compositor_catches_late_move_nothing(percent);
    for (const double hz : {60.0, 144.0, 360.0}) bursts_of_late_frames_move_nothing(hz);
    wandering_commits_at_360_hertz_stay_clear_of_the_latch();
    for (const std::int64_t width : {1'500'000LL, 2'000'000LL, 2'500'000LL}) {
        commits_wandering_over_the_latch_now_and_then_are_not_the_latch_moving(width);
    }
    sustained_overload_sweeps_once_a_minute_at_most();
    a_latch_that_keeps_moving_is_swept_for_once_a_minute_at_most();
    two_refresh_rates_in_one_process_are_swept_once_each();
    a_guest_that_is_not_a_multiple_of_the_output_is_never_timed();
    a_guest_that_changes_its_rate_is_started_over();
    an_interrupted_sweep_starts_over();
    a_blank_before_the_device_took_a_timing_does_not_restart_a_sweep();
    a_sweep_is_finished_across_bursts_of_frames();
    a_composed_time_that_wanders_does_not_restart_a_sweep();
    frames_without_their_stamps_say_nothing();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("latency: all checks passed\n");
    return EXIT_SUCCESS;
}
