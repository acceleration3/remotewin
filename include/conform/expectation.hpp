#pragma once

#include "host/mirror/mirror_state.hpp"
#include "host/placement/screen_mapping.hpp"
#include "proto/probe/window_truth.hpp"

#include <QString>

#include <optional>
#include <vector>

namespace rwin::conform {

// What Windows says a window is, turned into what its mirror has to be -
// one row per behaviour, each citing the rule it comes from.
//
// Written out here on purpose rather than asked of Win32Style: the mirror
// decides through Win32Style, and a check that asked the same class would
// agree with every mistake in it. The rows read the truth's words and
// state directly, and a row that is wrong here and right there is a
// disagreement worth reading either way.
class Expectation {
public:
    struct Check {
        QString property;
        // The Windows behaviour the expectation comes from.
        QString rule;
        QString expected;
        QString actual;
        bool passed = false;
        // Set on a row the mirror cannot pass, for a reason below it -
        // what Wayland or Qt cannot do (docs/conformance.md, known
        // limits). Such a row is listed, and fails nothing.
        QString known_limit{};

        [[nodiscard]] bool failed() const noexcept { return !passed && known_limit.isEmpty(); }
    };

    // ``as_made`` is the truth read the moment the window was made, before
    // the mirror had seen it; ``truth`` is the one read once the mirror has
    // settled. ``owner`` is the truth of the window that owns this one,
    // when the probe made that too. ``casts_shadow`` is whether a shadow
    // was seen around the window on the guest's screen, when that could be
    // looked at; ``drop_shadows`` is the desktop's SPI_GETDROPSHADOW, by
    // which Windows' own rule says the same where nothing could be seen.
    // ``screen`` is where the guest's screen is on this desktop; without it
    // where a mirror was put is not judged.
    Expectation(WindowTruth as_made, WindowTruth truth, std::optional<WindowTruth> owner,
                std::optional<bool> casts_shadow, bool drop_shadows,
                std::optional<host::ScreenMapping> screen = std::nullopt);

    // Windows shows a window that is visible; one never shown is not on
    // screen and nothing should stand for it.
    [[nodiscard]] bool wants_mirror() const noexcept { return truth_.shown; }

    // Every row that applies to this window, against ``mirror``.
    [[nodiscard]] std::vector<Check> against(const host::MirrorState& mirror,
                                             const host::MirrorState* owner_mirror) const;

    // What ``expected`` and ``actual`` are worked out from, together.
    struct Subject {
        const WindowTruth& as_made;
        const WindowTruth& truth;
        const WindowTruth* owner;
        std::optional<bool> casts_shadow;
        bool drop_shadows;
        const host::MirrorState& mirror;
        const host::MirrorState* owner_mirror;
        const host::ScreenMapping* screen;
    };

private:
    WindowTruth as_made_;
    WindowTruth truth_;
    std::optional<WindowTruth> owner_;
    std::optional<bool> casts_shadow_;
    bool drop_shadows_;
    std::optional<host::ScreenMapping> screen_;
};

}  // namespace rwin::conform
