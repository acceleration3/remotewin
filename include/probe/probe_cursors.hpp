#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct HICON__;

namespace rwin::probe {

// The cursors a scenario names: Windows' own by IDC_ number, and the ones
// the probe makes to be unlike any of those - a colour one with soft alpha
// edges, a monochrome one with a middle that inverts, an animated one
// whose steps last different times, and one larger than the system's.
//
// Made once and kept, as an application keeps its cursors; destroyed with
// the probe.
class ProbeCursors {
public:
    // What the animated one is made of, for a reader of its truth.
    static constexpr std::uint32_t kAnimatedSteps = 4;
    static constexpr std::uint32_t kAnimatedSide = 32;
    static constexpr std::uint32_t kLargeSide = 96;

    ProbeCursors();
    ~ProbeCursors();

    ProbeCursors(const ProbeCursors&) = delete;
    ProbeCursors& operator=(const ProbeCursors&) = delete;
    ProbeCursors(ProbeCursors&&) = delete;
    ProbeCursors& operator=(ProbeCursors&&) = delete;

    // An IDC_ number or a ProbeAction::MadeCursor. A null cursor
    // (MadeCursor::None) is an answer, not a failure: nothing when the
    // code names no cursor at all, with ``failure`` saying so.
    [[nodiscard]] std::optional<HICON__*> cursor_for(std::uint32_t code, std::string& failure) const;

private:
    [[nodiscard]] static HICON__* make_colour(std::uint32_t side, std::uint32_t hotspot);
    [[nodiscard]] static HICON__* make_monochrome();
    [[nodiscard]] static HICON__* load_animated(std::string& failure);

    // Before the cursors: the animated one says here why it was not made.
    std::string animated_failure_;
    HICON__* colour_ = nullptr;
    HICON__* monochrome_ = nullptr;
    HICON__* animated_ = nullptr;
    HICON__* large_ = nullptr;
};

}  // namespace rwin::probe
