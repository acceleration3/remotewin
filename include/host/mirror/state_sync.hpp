#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace rwin::host {

// One state of a window - maximised, minimised - kept the same on both
// sides of a mirror. A change one side makes is asked of the other; the
// other's answer, when it comes, is that ask landing and not a change of
// its own to send back; and what either side says while an ask of it is on
// its way - a report written before it arrived - is not taken for a change
// at all. Without that, a guest still reporting the old state undoes a
// maximise the compositor just made, and the two flip each other back.
//
// A change is an edge: a side that says again what it said is saying
// nothing new. An ask not answered within kTransit is given up on, and the
// side is believed again.
class StateSync {
public:
    using Ask = std::function<void(bool)>;

    StateSync(Ask ask_guest, Ask ask_compositor);

    // What each side says the window is now; ``at`` is now_ns.
    void guest_says(bool state, std::int64_t at);
    void compositor_says(bool state, std::int64_t at);
    // The guest is to be in ``state`` for a reason of this side's own (the
    // compositor activated a window the guest has minimised): asked unless
    // it is so, or already asked.
    void want_guest(bool state, std::int64_t at);

    // What each side is, or is on its way to being.
    [[nodiscard]] bool guest_expected() const noexcept;
    [[nodiscard]] bool compositor_expected() const noexcept;
    // An ask of the guest has not landed yet.
    [[nodiscard]] bool guest_heading() const noexcept { return guest_.asked.has_value(); }

    // A new surface: the compositor knows nothing of what it was asked, and
    // is asked again for what the guest has.
    void compositor_anew(std::int64_t at);

private:
    struct Side {
        std::optional<bool> seen;
        std::optional<bool> asked;
        std::int64_t asked_at = 0;
    };

    // Long enough for either side to act on an ask and say so.
    static constexpr std::int64_t kTransitNs = 1'000'000'000;

    // True when what was said is the answer to an ask, or said while one
    // is on its way.
    static bool answers_ask(Side& side, bool state, std::int64_t at);
    static void ask(Side& side, bool state, std::int64_t at, const Ask& asking);

    Ask ask_guest_;
    Ask ask_compositor_;
    Side guest_;
    Side compositor_;
};

}  // namespace rwin::host
