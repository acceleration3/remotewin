#pragma once

#include <cstdint>

namespace rwin::host {

// Waiting for the guest's ``copies``-th copy into a surface to have run on
// the host: what each of the GPU device's answers to a sync means.
//
// The guest names a frame as soon as it has asked its GPU for the copy, and
// its command may still be in the guest's ring when the host is asked. The
// device never holds an answer back, so a count short of the copy is asked
// again a moment later - up to a limit, past which the copy is not coming
// (the guest let go of the surface, or its GPU was reset) and the frame is
// dropped.
class CopyWait {
public:
    enum class Verdict {
        // Run: the fence that came with the answer covers it.
        Ready,
        AskAgain,
        GiveUp,
    };

    // A millisecond between asks: a copy waits on nothing but the guest's
    // ring, which the host empties as fast as the guest fills it.
    static constexpr int kRetryMs = 1;
    // Two seconds' worth of asking.
    static constexpr int kMostAsks = 2000;

    explicit CopyWait(std::uint64_t copies) noexcept : copies_(copies) {}

    [[nodiscard]] std::uint64_t copies() const noexcept { return copies_; }

    // The device has run ``copies_run`` copies into the surface.
    [[nodiscard]] Verdict answered(std::uint64_t copies_run) noexcept;

private:
    std::uint64_t copies_;
    int asks_ = 0;
};

}  // namespace rwin::host
