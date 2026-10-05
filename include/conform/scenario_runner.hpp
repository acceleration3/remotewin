#pragma once

#include "conform/input_expectation.hpp"
#include "conform/scenario.hpp"
#include "conform/scenario_outcome.hpp"
#include "host/link/guest_window_info.hpp"
#include "host/mirror/mirror_state.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/probe/input_record.hpp"
#include "proto/probe/window_truth.hpp"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QRect>
#include <QRegion>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class QWindow;

namespace rwin::host {
class GuestLink;
class MirrorSession;
class MirrorWindow;
}  // namespace rwin::host

namespace rwin::conform {

class ProbeLink;

// Runs one scenario end to end: the probe makes the windows, the mirror
// catches up with them, both sides are looked at while neither is moving,
// the input is put in, and everything is taken down again before the next.
class ScenarioRunner {
public:
    struct Settings {
        QColor backdrop;
        // The guest's work area: where windows can be, and what a capture
        // is kept within. Past it is the taskbar, which is neither side's
        // window.
        QRect work_area;
        int channel_tolerance = 0;
        // SPI_GETDROPSHADOW, as the probe found it.
        bool drop_shadows = false;
        // What the guest's shell draws over every window - the "Activate
        // Windows" watermark of an unactivated copy - in screen pixels:
        // seen over the backdrop alone, before any scenario. Neither
        // side's window, so not compared.
        QRegion shell_overlay;
    };

    ScenarioRunner(ProbeLink& probe, host::MirrorSession& mirrors, host::GuestLink& link,
                   Settings settings);

    [[nodiscard]] ScenarioOutcome run(const Scenario& scenario);

private:
    [[nodiscard]] bool make_windows(const Scenario& scenario, ScenarioOutcome& outcome);
    [[nodiscard]] static QString signature_of(const Scenario& scenario, const ScenarioOutcome& outcome);
    void skip_if_seen(const Scenario& scenario, ScenarioOutcome& outcome);
    [[nodiscard]] bool read_truths(ScenarioOutcome& outcome);
    [[nodiscard]] bool settle(const ScenarioOutcome& outcome);
    [[nodiscard]] QList<qint64> signature(const ScenarioOutcome& outcome) const;
    void look(ScenarioOutcome& outcome);
    void lay_host_scene(ScenarioOutcome& outcome);
    [[nodiscard]] std::optional<bool> shadow_around(const ScenarioOutcome& outcome,
                                                    QRect visible) const;
    void check_properties(ScenarioOutcome& outcome);
    void check_pointer(ScenarioOutcome& outcome);
    void put_motion(const Scenario::Step& step, const ScenarioOutcome::Window& window,
                    std::vector<ScenarioOutcome::Input>& checks);
    void put_input(const Scenario& scenario, ScenarioOutcome& outcome);
    void put_step(const Scenario::Step& step, ScenarioOutcome& outcome,
                  std::vector<ScenarioOutcome::Input>& checks);
    void run_steps(const Scenario& scenario, ScenarioOutcome& outcome);
    void take_step(const Scenario::Step& step, ScenarioOutcome& outcome,
                   ScenarioOutcome::Stage& stage);
    void act(const Scenario::Step& step, ScenarioOutcome& outcome, ScenarioOutcome::Stage& stage);
    void as_user_here(const Scenario::Step& step, ScenarioOutcome& outcome, ScenarioOutcome::Stage& stage);
    // Settled, read, looked at and checked, as the scenario first was.
    void look_again(ScenarioOutcome& outcome, QStringList& problems);
    [[nodiscard]] static ScenarioOutcome::Stage snapshot(const ScenarioOutcome& outcome);
    static void restore(ScenarioOutcome& outcome, const ScenarioOutcome::Stage& stage);
    void deliver(const Scenario::Step& step, const WindowTruth& truth, host::MirrorWindow& mirror);
    void press_keys(const Scenario::Step& step, QWindow* surface);
    [[nodiscard]] static InputExpectation::Check activation(const WindowTruth& before,
                                                            const WindowTruth& after);
    [[nodiscard]] std::optional<host::MirrorState> brought_forward(const std::optional<WindowTruth>& clicked) const;
    [[nodiscard]] InputExpectation::Check forward_check(const host::MirrorState& before,
                                                        const WindowTruth& clicked,
                                                        const ScenarioOutcome& outcome) const;
    void note_guest_click(const ScenarioOutcome& outcome, ScenarioOutcome::Stage& stage);
    [[nodiscard]] InputExpectation::Check move_on_host(const Scenario::Step& step,
                                                       const ScenarioOutcome& outcome);
    [[nodiscard]] InputExpectation::Check resize(const Scenario::Step& step,
                                                 const ScenarioOutcome::Window& window,
                                                 host::MirrorWindow& mirror);
    // What the probe's windows were given until ``satisfied`` says it is
    // all there, or for as long as it might take to get there.
    template <typename Satisfied>
    [[nodiscard]] std::optional<std::vector<InputRecord>> collect(Satisfied satisfied, QString& error);
    void tear_down(ScenarioOutcome& outcome);

    ProbeLink& probe_;
    host::MirrorSession& mirrors_;
    Settings settings_;
    // What the mirror's guest half last said about each window.
    QHash<quint64, host::GuestWindowInfo> reported_;
    // Generated scenarios run so far, by what their windows came out as.
    QHash<QString, QString> seen_;
    int timestamp_ = 0;
    // The scenario at hand looks at the cursor and the pointer; the window
    // its pointer was last put over, by place; and what was wanted held at
    // the last look, which a look that wants nothing releases.
    bool pointer_ = false;
    int pointer_window_ = 0;
    PointerGrab::Kind held_before_ = PointerGrab::Kind::None;
    // Why the user on this desktop had the pointer let go - the mirror
    // lost the focus, the escape was pressed - until it is taken back.
    QString let_go_;
};

}  // namespace rwin::conform
