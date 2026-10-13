#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rwin::host {

struct FrameStamps;

// Whether the frames shown kept the rhythm the guest composed them in: for
// each frame and the one before, the guest's blanks between them against the
// output's refreshes between them. Equal is smooth; a frame put on screen
// more refreshes after the one before than the guest composed it is held - a
// stutter - and fewer is rushed, the one before seen for too short a time or
// not at all.
class CadenceMeter {
public:
    void take(const FrameStamps& frame) noexcept;

    [[nodiscard]] std::size_t steady() const noexcept { return steady_; }
    [[nodiscard]] std::size_t held() const noexcept { return held_; }
    [[nodiscard]] std::size_t rushed() const noexcept { return rushed_; }
    // Blanks between two frames shown with no frame of their own: the guest
    // composed none, or the one it composed went no further than here (one
    // drawn over before it was drawn, or one the compositor discarded).
    [[nodiscard]] std::size_t unshown_blanks() const noexcept { return unshown_blanks_; }
    [[nodiscard]] std::int64_t longest_on_screen_ns() const noexcept { return longest_ns_; }

    // "cadence 290 steady, 4 held, 1 rushed; 3 blanks without a frame;
    // longest on screen 33.3 ms".
    [[nodiscard]] std::string summary() const;
    // Counted afresh from here; the frame before is still the one before.
    void restart() noexcept;

private:
    std::int64_t composed_ = 0;
    std::int64_t presented_ = 0;
    std::size_t steady_ = 0;
    std::size_t held_ = 0;
    std::size_t rushed_ = 0;
    std::size_t unshown_blanks_ = 0;
    std::int64_t longest_ns_ = 0;
};

}  // namespace rwin::host
