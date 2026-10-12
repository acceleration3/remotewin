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
// One number is steered: how long before one of this output's blanks the
// guest blanks (ahead). Its period is the whole number of this output's
// periods nearest its own, so every frame of it lands at the same point of
// this output's period; a frame that makes the blank it was put before is
// light exactly ahead after its guest blank, and one that misses it a
// period later. ahead starts at what nine frames in ten took unlocked -
// never worse than before - and comes down a step a second while no frame
// misses; once frames miss, it goes back up two steps and stays at least
// there.
class DisplayLock {
public:
    struct Timing {
        std::uint32_t refresh_millihz = 0;
        // A moment on now_ns's clock the guest is to blank in phase with.
        std::int64_t blank = 0;

        [[nodiscard]] bool operator==(const Timing&) const noexcept = default;
    };

    // The GPU device's display modes are all 60 Hz (accel-virt's
    // DisplayMode): the guest is not told of another rate, so it keeps one
    // near that.
    static constexpr std::int64_t kGuestPeriodNs = 16'666'667;
    // Frames weighed before the first timing, and in each judgement after.
    static constexpr std::size_t kFrames = 120;
    static constexpr std::size_t kFewestFrames = 60;
    static constexpr std::int64_t kEveryNs = 1'000'000'000;
    // More frames than this, in percent, missing their blank is too many.
    static constexpr std::size_t kMostLatePercent = 5;
    // A phase this close to the one asked for already is not asked again.
    static constexpr std::int64_t kSamePhaseNs = 100'000;

    // ``frame`` reached the screen, and it is ``now``: the timing to ask
    // for, when a new one is due.
    [[nodiscard]] std::optional<Timing> take(const FrameStamps& frame, std::int64_t now);

    // How long before this output's blank the guest's is put; 0 before the
    // first timing.
    [[nodiscard]] std::int64_t ahead() const noexcept { return ahead_; }

    // What was asked for may have been undone - another mirror timed the
    // guest's display meanwhile - so the next timing due is asked for
    // whatever it is.
    void forget() noexcept { asked_.reset(); }

private:
    void restart(std::int64_t refresh, std::int64_t now) noexcept;
    void steer() noexcept;
    [[nodiscard]] std::int64_t step() const noexcept;
    [[nodiscard]] std::int64_t unlocked_took() const;
    [[nodiscard]] Timing timing_from(std::int64_t presented) const noexcept;
    [[nodiscard]] bool same_phase(const Timing& timing) const noexcept;

    std::int64_t refresh_ = 0;
    std::int64_t ahead_ = 0;
    // The least ahead may come down to: where frames last missed, or 0.
    std::int64_t floor_ = 0;
    std::array<std::int64_t, kFrames> took_{};
    std::size_t taken_ = 0;
    std::size_t judged_ = 0;
    std::size_t late_ = 0;
    std::int64_t next_ = 0;
    std::optional<Timing> asked_;
    // When asked_ was asked for: frames the guest composed before are
    // nothing to judge it by.
    std::int64_t asked_at_ = 0;
};

}  // namespace rwin::host
