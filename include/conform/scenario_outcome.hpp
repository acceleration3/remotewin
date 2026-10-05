#pragma once

#include "conform/expectation.hpp"
#include "conform/input_expectation.hpp"
#include "conform/pixel_comparison.hpp"
#include "host/link/guest_window_info.hpp"
#include "host/mirror/mirror_state.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/probe/cursor_truth.hpp"
#include "proto/probe/window_truth.hpp"

#include <QImage>
#include <QRect>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::conform {

// Everything one scenario found: per window, the truth, the mirror's
// state, the property checks and the pixel comparison; the input checks;
// and the scene both sides painted.
struct ScenarioOutcome {
    struct Window {
        int index = 0;
        QString title;
        std::uint64_t handle = 0;
        // Read as soon as the probe had made it, and again once the mirror
        // had settled.
        std::optional<WindowTruth> as_made;
        std::optional<WindowTruth> truth;
        std::optional<host::MirrorState> mirror;
        // What the mirror's guest half last reported for it, as it arrived.
        std::optional<host::GuestWindowInfo> reported;
        std::optional<bool> casts_shadow;
        std::vector<Expectation::Check> checks;
        std::optional<PixelComparison> pixels;
        // The mirror's picture was not one to one and was resampled to
        // be compared.
        bool resampled = false;
    };

    struct Input {
        QString step;
        InputExpectation::Check check;
    };

    // The cursor and the pointer once both sides settled, in a scenario
    // that looks at them: Windows' account, the rows, what the application
    // wanted held, and the cursor as each side shows it, over white.
    struct Pointer {
        std::optional<CursorTruth> truth;
        // The window the pointer is over, by its place in the scenario.
        int window = 0;
        std::vector<Expectation::Check> checks;
        PointerGrab::Kind wanted = PointerGrab::Kind::None;
        QImage guest;
        QImage host;
    };

    // Both sides as one step of a scenario's ``steps`` left them, once
    // they had settled: every window, the input checks of the step, and
    // the picture.
    struct Stage {
        QString step;
        std::vector<Window> windows;
        std::vector<Input> input;
        QStringList problems;
        // What the guest did that no row compares but a reader wants to
        // know: how Windows answered a click it was given directly.
        QStringList notes;
        QRect scene;
        QImage guest;
        QImage host;
        std::optional<PixelComparison> scene_pixels;
        std::optional<Pointer> pointer;
    };

    QString name;
    QString about;
    std::vector<Window> windows;
    std::vector<Stage> stages;
    std::vector<Input> input;
    // What kept the scenario from being looked at properly: a window the
    // probe could not make, a mirror that never settled. A scenario with
    // any has failed, whatever else it found.
    QStringList problems;
    // The part of the guest's screen both pictures are of.
    QRect scene;
    QImage guest;
    QImage host;
    std::optional<PixelComparison> scene_pixels;
    std::optional<Pointer> pointer;
    // How many looks it took to see the two sides at rest at once.
    int looks = 0;
    // Why a generated scenario was not looked at: Windows would not make
    // its windows, or made them into words an earlier scenario had.
    QString skipped;

    [[nodiscard]] bool passed() const;
    // The property names, input steps and pixel comparisons that failed.
    [[nodiscard]] QStringList failures() const;
    // The rows that did not pass for a reason known and written down.
    [[nodiscard]] QStringList known_limits() const;
};

}  // namespace rwin::conform
