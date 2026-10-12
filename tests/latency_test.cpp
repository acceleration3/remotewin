// A frame's time, stage by stage: the guest's clock put on this side's from
// probes, trusting the one that crossed fastest; and the meter's spread of
// each stage, skipping stages a frame was not seen at.
#include "host/latency/clock_offset.hpp"
#include "host/latency/display_lock.hpp"
#include "host/latency/frame_stamps.hpp"
#include "host/latency/latency_meter.hpp"

#include <cstdio>
#include <optional>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using Stage = rwin::host::FrameStamps::Stage;

// The guest's clock is this side's plus 5 ms, in 100 ns ticks.
constexpr std::int64_t kAhead = 5'000'000;

std::int64_t guest_at(std::int64_t host) { return (host + kAhead) / 100; }

void the_fastest_answer_is_trusted() {
    rwin::host::ClockOffset offset;
    check(!offset.to_host(guest_at(1'000'000)).has_value(), "nothing is known before an answer");
    // Answered at the middle of a 200 us round trip: exact.
    offset.take(1'000'000, guest_at(1'100'000), 1'200'000);
    check(offset.to_host(guest_at(9'000'000)) == 9'000'000, "an answer from the middle puts the guest exactly");
    // A slower answer that waited on the guest: it says the guest is later
    // than it is, and is not trusted over the fast one.
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
    // The guest's clock moves 1 ms; every later answer is slower than the
    // first but has to win once it has fallen out of the window.
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
    stamps.note(Stage::Drawn, drawn);
    return stamps;
}

void each_stage_is_timed_from_the_one_before() {
    rwin::host::LatencyMeter meter;
    for (std::int64_t at = 1; at <= 19; ++at) {
        meter.take(frame(at * 1'000'000, at * 1'000'000 + 3'000'000, at * 1'000'000 + 4'000'000));
    }
    // Two slow frames of 21: the 95th percentile is the 20th.
    meter.take(frame(100'000'000, 100'000'000 + 30'000'000, 100'000'000 + 31'000'000));
    meter.take(frame(200'000'000, 200'000'000 + 30'000'000, 200'000'000 + 31'000'000));
    check(meter.frames() == 21, "every frame is counted");
    const auto received = meter.to(Stage::Received);
    check(received.has_value() && received->median == 3'000'000, "received is timed from composed");
    check(received.has_value() && received->p95 == 30'000'000, "the slow frames are the 95th percentile");
    const auto drawn = meter.to(Stage::Drawn);
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

// A compositor at 60 Hz whose blanks fall 1 ms past each whole period, and
// which shows a commit at the first blank at least 3 ms after it.
constexpr std::int64_t kHostPeriod = 16'666'667;
constexpr std::int64_t kHostPhase = 1'000'000;
constexpr std::int64_t kLatch = 3'000'000;

std::int64_t light_for(std::int64_t swapped) {
    const std::int64_t earliest = swapped + kLatch - kHostPhase;
    return (earliest + kHostPeriod - 1) / kHostPeriod * kHostPeriod + kHostPhase;
}

// A guest whose display clock does what the lock asks of it, from the blank
// after the one it is at, and whose frames take ``pipeline`` to this side's
// swap. Its clock starts a little fast, so its frames come at every phase.
class Guest {
public:
    explicit Guest(rwin::host::DisplayLock& lock) : lock_(lock) {}

    // ``frames`` frames shown: how many timings were asked for, and how
    // many frames missed the blank they were meant for.
    struct Seen {
        int timings = 0;
        int late = 0;
    };
    Seen show(int frames, std::int64_t pipeline) {
        Seen seen;
        for (int frame = 0; frame < frames; ++frame) {
            const std::int64_t light = light_for(blank_ + pipeline);
            if (timed_ && light - blank_ > lock_.ahead() + kHostPeriod / 2) ++seen.late;
            rwin::host::FrameStamps stamps;
            stamps.note(Stage::Composed, blank_);
            stamps.note(Stage::Swapped, blank_ + pipeline);
            stamps.note(Stage::Presented, light);
            stamps.refresh_ns = kHostPeriod;
            const auto timing = lock_.take(stamps, light);
            blank_ += period_;
            if (!timing.has_value()) continue;
            ++seen.timings;
            timed_ = true;
            period_ = 1'000'000'000'000 / timing->refresh_millihz;
            // The next blank in phase with the one asked for.
            const std::int64_t behind = ((blank_ - timing->blank) % period_ + period_) % period_;
            blank_ += behind == 0 ? 0 : period_ - behind;
        }
        return seen;
    }

private:
    rwin::host::DisplayLock& lock_;
    std::int64_t blank_ = 1'000'000'000;
    std::int64_t period_ = 16'600'000;
    bool timed_ = false;
};

void the_guest_is_brought_just_ahead_of_the_deadline_and_kept_there() {
    rwin::host::DisplayLock lock;
    Guest guest{lock};
    const Guest::Seen unlocked = guest.show(120, 4'000'000);
    check(unlocked.timings == 1, "a timing is asked for once enough frames were seen");
    const std::int64_t first = lock.ahead();
    check(first > 7'000'000 + kHostPeriod / 2, "it starts where nine frames in ten were, unlocked");
    // A step down a second while nothing is late, until the frames are put
    // so close that they miss: then back up, and kept there.
    const Guest::Seen settling = guest.show(60 * 30, 4'000'000);
    check(settling.timings > 3, "it came down a step at a time");
    // The deadline is 3 ms before the blank and the pipeline 4 ms: 7 ms is
    // the least that makes it, and it settles at most three steps above.
    check(lock.ahead() >= 7'000'000 && lock.ahead() <= 7'000'000 + 3 * 1'000'000, "it settles just ahead of the deadline");
    const std::int64_t settled = lock.ahead();
    const Guest::Seen kept = guest.show(60 * 30, 4'000'000);
    check(kept.late == 0 && kept.timings == 0, "settled, nothing is late and nothing new is asked for");
    check(lock.ahead() == settled, "settled, it stays");
    // The guest slows by 2 ms: its frames miss, and it is put further ahead.
    guest.show(60 * 10, 6'000'000);
    check(lock.ahead() > settled, "frames that miss put the guest further ahead");
    const Guest::Seen slower = guest.show(60 * 10, 6'000'000);
    check(slower.late == 0, "and then they make it");
}

void a_fast_output_gives_the_guest_the_whole_number_of_its_periods_nearest_sixty_hertz() {
    rwin::host::DisplayLock lock;
    std::optional<rwin::host::DisplayLock::Timing> timing;
    for (int frame = 0; frame < 200 && !timing.has_value(); ++frame) {
        rwin::host::FrameStamps stamps;
        stamps.note(Stage::Composed, 1'000'000'000 + frame * 16'000'000);
        stamps.note(Stage::Swapped, 1'004'000'000 + frame * 16'000'000);
        stamps.note(Stage::Presented, 1'008'000'000 + frame * 16'000'000);
        stamps.refresh_ns = 2'777'778;   // 360 Hz
        timing = lock.take(stamps, stamps.of(Stage::Presented));
    }
    check(timing.has_value() && timing->refresh_millihz == 60000, "six 360 Hz periods are 60 Hz");
}

rwin::host::FrameStamps shown(std::int64_t composed, std::int64_t pipeline) {
    rwin::host::FrameStamps stamps;
    stamps.note(Stage::Composed, composed);
    stamps.note(Stage::Swapped, composed + pipeline);
    stamps.note(Stage::Presented, light_for(composed + pipeline));
    stamps.refresh_ns = kHostPeriod;
    return stamps;
}

void frames_without_their_stamps_say_nothing() {
    rwin::host::DisplayLock lock;
    rwin::host::FrameStamps unpresented = shown(1'000'000'000, 4'000'000);
    unpresented.note(Stage::Presented, 0);
    for (int frame = 0; frame < 200; ++frame) {
        check(!lock.take(unpresented, 1'000'000'000 + frame).has_value(), "a frame never presented times nothing");
    }
    rwin::host::FrameStamps unrefreshed = shown(1'000'000'000, 4'000'000);
    unrefreshed.refresh_ns = 0;
    check(!lock.take(unrefreshed, 2'000'000'000).has_value(), "an output with no fixed refresh times nothing");
}

}  // namespace

int main() {
    the_fastest_answer_is_trusted();
    old_answers_are_let_go();
    each_stage_is_timed_from_the_one_before();
    a_frame_seen_nowhere_is_not_counted();
    the_guest_is_brought_just_ahead_of_the_deadline_and_kept_there();
    a_fast_output_gives_the_guest_the_whole_number_of_its_periods_nearest_sixty_hertz();
    frames_without_their_stamps_say_nothing();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("latency: all checks passed\n");
    return EXIT_SUCCESS;
}
