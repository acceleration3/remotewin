#pragma once

#include "host/latency/frame_stamps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rwin::host {

// The timing the guest's display is to have so that each frame it composes
// reaches this side's compositor just before the compositor takes what it
// shows next, rather than anywhere in the period before.
//
// A closed loop on one number, the commit phase: where a frame's commit falls
// against the compositor's latch, which the guest's display is shifted to
// move. The rate is the guest's own, as the device says it with each frame,
// and is never asked to change: Windows is told that rate, and a clock
// blanking at another is an application counting blanks running fast. It is a
// lock only where the guest's period is the whole number of this output's
// periods nearest it (kMostRateOffPart): then every frame of it lands at the
// same point of this output's period. Where it is not - a 144 Hz guest on a
// 240 Hz output, a 60 Hz guest on a 59.94 Hz one - a frame's commit slides
// round the period and no shift holds it, so nothing is asked for and the
// frames are shown as the compositor takes them. A commit a hair either side of
// the latch is on screen a whole refresh sooner or later, so commits are kept
// a margin before it, far enough that their wandering cannot cross it. Nothing
// says where the latch is, but what a commit takes to reach the screen bounds
// it: one that made it took the latch, one that just missed it a refresh more.
// The phase is swept once across a refresh so that some do both, and the latch
// is the least any step of it took, and is not touched after. It is swept again
// only when two windows in a row cannot be what that latch gives, and no
// oftener than kLeastBetweenSweepsNs: a compositor that misses blanks of its
// own shows the same commits a refresh late, which is no news of the latch.
// Every timing asked for makes the guest's compositor drop frames for a fifth
// of a second, so none is asked for while commits are within the margin's
// deadband. A guest that changes its rate is started over: what was learnt
// of its cadence says nothing of the new one.
//
// A process's windows may be on outputs of different refresh, and focus moves
// between them: each output is known by its stated refresh and keeps what was
// learnt of it, so only the first visit sweeps.
class DisplayLock {
public:
    struct Timing {
        // How much earlier than its schedule the guest's display is to
        // blank; negative: later.
        std::int64_t shift_ns = 0;

        [[nodiscard]] bool operator==(const Timing&) const noexcept = default;
    };

    // The guest's period is a whole number of the output's within this part in
    // a million, or the frames slide: a stated refresh is a millihertz the
    // monitor's own differs from by ten parts at most, and a guest at 59.94 on
    // a 60 output is 1000.
    static constexpr std::int64_t kMostRateOffPpm = 50;
    // Frames weighed before the first timing, and in each judgement after.
    static constexpr std::size_t kFrames = 120;
    static constexpr std::size_t kFewestFrames = 60;
    static constexpr std::int64_t kEveryNs = 1'000'000'000;
    // The sweep's steps need only long enough to see frames at each phase.
    static constexpr std::size_t kFewestSweepFrames = 15;
    static constexpr std::int64_t kSweepEveryNs = 250'000'000;
    // What the guest's frames take for a moment after a retiming is no
    // measure of where it was put.
    static constexpr std::int64_t kSettleNs = 3'000'000'000;
    // Frames further apart than this were not shown in sequence: the window
    // was covered or not the one in use, and another mirror may have moved
    // the guest's display meanwhile.
    static constexpr std::int64_t kMostGapNs = 250'000'000;
    // A sweep that a break in the frames interrupted goes on if the guest's
    // blanks are where its own timings left them, to this much: a composed
    // time wanders by a tenth of it. Further, another mirror moved them.
    static constexpr std::int64_t kGridMovedNs = 300'000;
    // A sweep is not begun again sooner after the last one ended, whatever the
    // windows look like: sustained overload is not the latch moving.
    static constexpr std::int64_t kLeastBetweenSweepsNs = 60'000'000'000;
    static constexpr std::size_t kOutputs = 4;

    // ``frame`` reached the screen, and it is ``now``, the guest's display
    // blanking at ``guest_microhz``: the timing to ask for, when a new one is
    // due. 0 for a guest that did not say its rate, which is not locked.
    [[nodiscard]] std::optional<Timing> take(const FrameStamps& frame, std::int64_t now, std::uint32_t guest_microhz);

private:
    // What is learnt of one output, by its stated refresh.
    struct Output {
        void begin(std::int64_t stated_refresh) noexcept;
        void begin_sweep() noexcept;
        [[nodiscard]] std::int64_t step() const noexcept;
        [[nodiscard]] bool sweeping() const noexcept { return sweep_left != 0; }

        // The output's refresh as its compositor states it; 0 for an entry
        // not claimed.
        std::int64_t refresh = 0;
        // What a commit made just in time takes to reach the screen.
        std::int64_t latch = 0;
        // The steps left of the sweep that finds the latch.
        std::size_t sweep_left = 0;
        // Judgements in a row that the latch cannot explain.
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
    // The rate the guest's display blanks at, in microhertz, and its period.
    std::uint32_t guest_microhz_ = 0;
    std::int64_t guest_period_ = 0;
    std::int64_t last_presented_ = 0;
    // When the last frame seen on the guest's present grid was composed, and
    // the shift asked since, which that grid's blanks are earlier by.
    std::int64_t last_composed_ = 0;
    std::int64_t unseen_shift_ = 0;
    std::array<std::int64_t, kFrames> commits_{};
    std::size_t judged_ = 0;
    std::int64_t next_ = 0;
    // When a timing that moves the phase was last asked for: frames the guest
    // composed before are nothing to judge it by.
    std::int64_t asked_at_ = 0;
};

}  // namespace rwin::host
