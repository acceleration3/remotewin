#include "conform/scenario_runner.hpp"

#include "conform/cursor_picture_on_glass.hpp"
#include "conform/host_scene.hpp"
#include "conform/motion_expectation.hpp"
#include "conform/pointer_expectation.hpp"
#include "conform/probe_link.hpp"
#include "conform/wait_until.hpp"
#include "host/chrome/win32_style.hpp"
#include "host/input/key_translator.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_session.hpp"
#include "host/mirror/mirror_window.hpp"
#include "host/mirror/pixel_mapping.hpp"
#include "host/placement/kwin_placement.hpp"
#include "host/placement/kwin_script.hpp"
#include "host/placement/screen_mapping.hpp"
#include "logging/logger.hpp"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QRegion>
#include <QStringList>
#include <QWindow>
#include <QtTest/qtestmouse.h>
#include <linux/input-event-codes.h>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace rwin::conform {

namespace {

using namespace std::chrono_literals;

constexpr std::chrono::milliseconds kSettleTimeout = 15s;
// Long enough for a frame or a window list from the guest to have
// arrived if one were coming: the guest half looks thirty times a second.
constexpr std::chrono::milliseconds kQuiet = 700ms;
// A window that must not be mirrored is given this long to be mirrored
// anyway.
constexpr std::chrono::milliseconds kAbsenceGrace = 1500ms;
constexpr std::chrono::milliseconds kRecheck = 300ms;
constexpr int kMaxLooks = 3;
// Captured around the windows, so the shadow can be seen.
constexpr int kShadowMargin = 16;
constexpr int kShadowBand = 8;
constexpr int kShadowDelta = 6;
constexpr double kShadowShare = 0.15;
constexpr int kShadowMinimumPixels = 64;
constexpr std::chrono::milliseconds kInputTimeout = 3s;
constexpr std::chrono::milliseconds kInputPoll = 150ms;
constexpr std::chrono::milliseconds kButtonGap = 40ms;
constexpr std::chrono::milliseconds kKeyGap = 20ms;
constexpr std::uint32_t kWsExLayoutRtl = 0x00400000;
constexpr std::uint32_t kWsExLayeredTransparent =
    host::Win32Style::kWsExLayered | host::Win32Style::kWsExTransparent;
constexpr std::chrono::milliseconds kTearDownTimeout = 5s;
// FlashWindowEx's flashes, at the caret blink rate, all done.
constexpr std::chrono::milliseconds kFlashTime = 3s;
constexpr std::uint32_t kWmActivate = 0x0006;
constexpr std::uint32_t kWmNcActivate = 0x0086;
constexpr std::uint32_t kWmLButtonDown = 0x0201;
// A held pointer's motion is fed as a mouse reports it, a few counts at a
// time.
constexpr int kMotionParts = 4;

bool on_screen(const WindowTruth& truth) { return truth.shown && !truth.iconic; }

QRect rect_of(const WindowTruth::Rect& rect) {
    return QRect(rect.left, rect.top, rect.width(), rect.height());
}

int delta(QRgb a, QRgb b) {
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                     std::abs(qBlue(a) - qBlue(b))});
}

QString action_name(const ProbeAction& action) {
    using Kind = ProbeAction::Kind;
    switch (action.kind) {
        case Kind::Enable:
            return action.on ? QStringLiteral("EnableWindow(TRUE)") : QStringLiteral("EnableWindow(FALSE)");
        case Kind::Show:
            return QStringLiteral("ShowWindow(%1)").arg(action.show);
        case Kind::SetOwner:
            return QStringLiteral("SetWindowLongPtr(GWLP_HWNDPARENT)");
        case Kind::Foreground:
            return QStringLiteral("SetForegroundWindow");
        case Kind::SetPosition:
            return QStringLiteral("SetWindowPos(%1,%2 %3x%4 z%5 flags %6)")
                .arg(action.x).arg(action.y).arg(action.width).arg(action.height)
                .arg(static_cast<int>(action.z)).arg(action.flags, 0, 16);
        case Kind::Move:
            return QStringLiteral("MoveWindow(%1,%2 %3x%4)").arg(action.x).arg(action.y).arg(action.width).arg(action.height);
        case Kind::OpenModal:
            return QStringLiteral("DialogBox");
        case Kind::OpenModeless:
            return QStringLiteral("CreateDialog");
        case Kind::EndDialog:
            return QStringLiteral("EndDialog");
        case Kind::ShowMessageBox:
            return QStringLiteral("MessageBox(flags %1)").arg(action.flags, 0, 16);
        case Kind::CloseMessageBox:
            return QStringLiteral("WM_CLOSE");
        case Kind::Destroy:
            return QStringLiteral("DestroyWindow");
        case Kind::Click:
            return QStringLiteral("SendInput click %1,%2").arg(action.x).arg(action.y);
        case Kind::ClassCursor:
            return QStringLiteral("SetClassLongPtr(GCLP_HCURSOR, %1)").arg(action.cursor);
        case Kind::AnswerCursor:
            return QStringLiteral("WM_SETCURSOR: SetCursor(%1)").arg(action.cursor);
        case Kind::ShowCursor:
            return QStringLiteral("ShowCursor(%1)").arg(action.on ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
        case Kind::ClipCursor:
            return action.width == 0 ? QStringLiteral("ClipCursor(NULL)")
                                     : QStringLiteral("ClipCursor(client %1,%2 %3x%4)").arg(action.x).arg(action.y)
                                           .arg(action.width).arg(action.height);
        case Kind::SetCursorPos:
            return QStringLiteral("SetCursorPos(client %1,%2)").arg(action.x).arg(action.y);
        case Kind::Capture:
            return action.on ? QStringLiteral("SetCapture") : QStringLiteral("ReleaseCapture");
        case Kind::RawInput:
            return action.on ? QStringLiteral("RegisterRawInputDevices(mouse, flags %1)").arg(action.flags, 0, 16)
                             : QStringLiteral("RegisterRawInputDevices(mouse, RIDEV_REMOVE)");
        case Kind::Recentre:
            return action.on ? QStringLiteral("SetCursorPos to the middle on every move") : QStringLiteral("stop putting the cursor back");
        case Kind::GuestMove:
            return QStringLiteral("SendInput move to client %1,%2").arg(action.x).arg(action.y);
        case Kind::ActivateShell:
            return QStringLiteral("SetForegroundWindow(taskbar)");
        case Kind::PointerPrecision:
            return QStringLiteral("Enhance pointer precision %1").arg(action.on ? QStringLiteral("on") : QStringLiteral("off"));
    }
    return {};
}

QString step_label(const Scenario::Step& step) {
    switch (step.kind) {
        case Scenario::Step::Kind::Move:
            return QStringLiteral("w%1 move %2,%3").arg(step.window).arg(step.client.x()).arg(step.client.y());
        case Scenario::Step::Kind::Click:
            return QStringLiteral("w%1 click %2,%3").arg(step.window).arg(step.client.x()).arg(step.client.y());
        case Scenario::Step::Kind::Key:
            return QStringLiteral("w%1 key %2").arg(step.window).arg(step.chord);
        case Scenario::Step::Kind::Resize:
            return QStringLiteral("w%1 resize %2x%3").arg(step.window).arg(step.size.width()).arg(step.size.height());
        case Scenario::Step::Kind::Act:
            return QStringLiteral("w%1 %2%3%4")
                .arg(step.window)
                .arg(action_name(step.action))
                .arg(step.other >= 0 ? QStringLiteral(" w%1").arg(step.other) : QString())
                .arg(step.action.title.empty() ? QString() : QStringLiteral(" '%1'").arg(QString::fromStdString(step.action.title)));
        case Scenario::Step::Kind::HostMove:
            return QStringLiteral("w%1 moved here to %2,%3").arg(step.window).arg(step.to.x()).arg(step.to.y());
        case Scenario::Step::Kind::Relative:
            return QStringLiteral("w%1 pointer moved by %2,%3 while held").arg(step.window).arg(step.motion.x()).arg(step.motion.y());
        case Scenario::Step::Kind::HostUnfocus:
            return QStringLiteral("w%1 mirror loses the focus here").arg(step.window);
        case Scenario::Step::Kind::HostFocus:
            return QStringLiteral("w%1 mirror gets the focus here").arg(step.window);
        case Scenario::Step::Kind::HostEscape:
            return QStringLiteral("w%1 Ctrl+Alt+G pressed on the mirror").arg(step.window);
        case Scenario::Step::Kind::HostRegrab:
            return QStringLiteral("w%1 mirror clicked to take the pointer back").arg(step.window);
    }
    return {};
}

}  // namespace

ScenarioRunner::ScenarioRunner(ProbeLink& probe, host::MirrorSession& mirrors, host::GuestLink& link,
                               Settings settings)
    : probe_(probe), mirrors_(mirrors), settings_(std::move(settings)) {
    QObject::connect(&link, &host::GuestLink::windows_listed, &link,
                     [this](const QList<host::GuestWindowInfo>& windows, bool, QSize) {
                         for (const host::GuestWindowInfo& window : windows) {
                             reported_.insert(window.handle, window);
                         }
                     });
}

ScenarioOutcome ScenarioRunner::run(const Scenario& scenario) {
    pointer_ = scenario.pointer;
    pointer_window_ = 0;
    held_before_ = PointerGrab::Kind::None;
    let_go_.clear();
    ScenarioOutcome outcome;
    outcome.name = scenario.name;
    outcome.about = scenario.about;
    RWIN_INFO("conform", "{}: {}", scenario.name.toStdString(), scenario.about.toStdString());
    const bool made = make_windows(scenario, outcome);
    if (!made && scenario.generated) {
        outcome.skipped = QStringLiteral("Windows would not make it: %1").arg(outcome.problems.join(QStringLiteral("; ")));
        outcome.problems.clear();
    }
    if (made && read_truths(outcome) && scenario.generated) skip_if_seen(scenario, outcome);
    if (made && outcome.skipped.isEmpty() && outcome.problems.isEmpty()) {
        for (ScenarioOutcome::Window& window : outcome.windows) window.as_made = window.truth;
        if (!settle(outcome)) outcome.problems.push_back(QStringLiteral("the mirrors did not settle"));
        // Settling is the mirror and the guest finishing with each other,
        // which can change the window: who has the foreground, for one.
        static_cast<void>(read_truths(outcome));
        for (int attempt = 1; attempt <= kMaxLooks; ++attempt) {
            outcome.looks = attempt;
            const QList<qint64> before = signature(outcome);
            look(outcome);
            idle_for(kRecheck);
            if (signature(outcome) == before) break;
            if (attempt == kMaxLooks) {
                outcome.problems.push_back(QStringLiteral("the mirrors kept changing while looked at"));
                break;
            }
            static_cast<void>(settle(outcome));
            static_cast<void>(read_truths(outcome));
        }
        check_properties(outcome);
        put_input(scenario, outcome);
        run_steps(scenario, outcome);
    }
    tear_down(outcome);
    return outcome;
}

// What a generated scenario's windows came out as: the words Windows kept
// and the state it put them in, with what the words do not say - the DWM
// attributes it was given and who owns whom.
QString ScenarioRunner::signature_of(const Scenario& scenario, const ScenarioOutcome& outcome) {
    QStringList parts;
    for (std::size_t i = 0; i < outcome.windows.size(); ++i) {
        const WindowTruth& truth = *outcome.windows[i].truth;
        const WindowSpec& spec = scenario.windows[i].spec;
        parts.push_back(QStringLiteral("%1/%2/%3/%4/%5/%6/%7/%8%9%10%11/%12/%13/%14")
                            .arg(truth.style, 0, 16)
                            .arg(truth.extended_style, 0, 16)
                            .arg(truth.class_style, 0, 16)
                            .arg(truth.layered_flags)
                            .arg(truth.layered_alpha)
                            .arg(truth.color_key, 0, 16)
                            .arg(static_cast<int>(truth.region))
                            .arg(truth.shown)
                            .arg(truth.iconic)
                            .arg(truth.zoomed)
                            .arg(truth.enabled)
                            .arg(scenario.windows[i].owner)
                            .arg(spec.corner_preference)
                            .arg(spec.dark_mode));
    }
    return parts.join(QLatin1Char('|'));
}

void ScenarioRunner::skip_if_seen(const Scenario& scenario, ScenarioOutcome& outcome) {
    const QString signature = signature_of(scenario, outcome);
    const auto earlier = seen_.constFind(signature);
    if (earlier == seen_.cend()) {
        seen_.insert(signature, scenario.name);
        return;
    }
    outcome.skipped = QStringLiteral("Windows made the same words as %1").arg(*earlier);
}

bool ScenarioRunner::make_windows(const Scenario& scenario, ScenarioOutcome& outcome) {
    for (std::size_t i = 0; i < scenario.windows.size(); ++i) {
        const Scenario::Window& wanted = scenario.windows[i];
        WindowSpec spec = wanted.spec;
        if (wanted.owner >= 0) spec.owner = outcome.windows[static_cast<std::size_t>(wanted.owner)].handle;
        QString error;
        const std::optional<std::uint64_t> handle = probe_.create_window(spec, error);
        if (!handle.has_value()) {
            outcome.problems.push_back(QStringLiteral("w%1 was not made: %2").arg(i).arg(error));
            return false;
        }
        ScenarioOutcome::Window made;
        made.index = static_cast<int>(i);
        made.title = QString::fromStdString(spec.title);
        made.handle = *handle;
        outcome.windows.push_back(std::move(made));
    }
    return true;
}

bool ScenarioRunner::read_truths(ScenarioOutcome& outcome) {
    for (ScenarioOutcome::Window& window : outcome.windows) {
        QString error;
        std::optional<WindowTruth> truth = probe_.observe(window.handle, error);
        if (!truth.has_value()) {
            outcome.problems.push_back(QStringLiteral("w%1 could not be observed: %2").arg(window.index).arg(error));
            return false;
        }
        window.truth = std::move(truth);
    }
    return true;
}

// Every mirror that should exist does and has shown a frame, and for a
// while nothing about any of them has changed.
bool ScenarioRunner::settle(const ScenarioOutcome& outcome) {
    const bool any_absent = std::any_of(outcome.windows.begin(), outcome.windows.end(),
                                        [](const auto& window) { return !window.truth->shown; });
    QElapsedTimer since;
    since.start();
    QElapsedTimer quiet;
    quiet.start();
    QList<qint64> last;
    return wait_until(
        [&] {
            for (const ScenarioOutcome::Window& window : outcome.windows) {
                if (!window.truth->shown) continue;
                const host::MirrorWindow* mirror = mirrors_.find(window.handle);
                if (mirror == nullptr) return false;
                if (on_screen(*window.truth) && mirror->state().frames_presented == 0) return false;
            }
            QList<qint64> now = signature(outcome);
            if (now != last) {
                last = std::move(now);
                quiet.restart();
                return false;
            }
            if (any_absent && !since.hasExpired(kAbsenceGrace.count())) return false;
            return quiet.hasExpired(kQuiet.count());
        },
        kSettleTimeout);
}

QList<qint64> ScenarioRunner::signature(const ScenarioOutcome& outcome) const {
    QList<qint64> signature;
    for (const ScenarioOutcome::Window& window : outcome.windows) {
        const host::MirrorWindow* mirror = mirrors_.find(window.handle);
        if (mirror == nullptr) {
            signature.push_back(-1);
            continue;
        }
        const host::MirrorState state = *mirrors_.state_of(window.handle);
        // Where the compositor says it put the window, and how many
        // placements were asked for: a placement still under way is a
        // mirror that has not settled.
        const QPointF frame = state.compositor_frame.value_or(QRectF()).topLeft();
        signature << static_cast<qint64>(state.frames_presented) << state.position.x()
                  << state.position.y() << state.logical_size.width() << state.logical_size.height()
                  << state.visible << state.minimized << state.maximized
                  << std::llround(state.device_pixel_ratio * 1000) << state.picture.x()
                  << state.picture.y() << state.picture.width() << state.picture.height()
                  << std::llround(frame.x() * 100) << std::llround(frame.y() * 100)
                  << state.placement_requests << state.guest_moves;
        if (!pointer_) continue;
        // The cursor the mirror wears and how it holds the pointer: what a
        // step of a pointer scenario changes. Not the steps of an animated
        // cursor, which never stop.
        const QRect held = state.pointer_hold.has_value() ? state.pointer_hold->region.boundingRect() : QRect();
        signature << static_cast<qint64>(state.cursor_id) << state.cursor_shape << state.cursor_pixels.width()
                  << (state.pointer_hold.has_value() ? static_cast<int>(state.pointer_hold->asked) : -1)
                  << held.x() << held.y() << held.width() << held.height();
    }
    return signature;
}

void ScenarioRunner::look(ScenarioOutcome& outcome) {
    QRect scene;
    for (const ScenarioOutcome::Window& window : outcome.windows) {
        if (on_screen(*window.truth)) scene |= rect_of(window.truth->visible);
    }
    outcome.scene = QRect();
    if (scene.isEmpty()) return;
    scene = scene.adjusted(-kShadowMargin, -kShadowMargin, kShadowMargin, kShadowMargin)
            & settings_.work_area;
    outcome.scene = scene;

    QString error;
    const std::optional<ScreenPixels> pixels = probe_.capture(scene, error);
    if (!pixels.has_value()) {
        outcome.problems.push_back(QStringLiteral("the guest's screen could not be read: %1").arg(error));
        return;
    }
    // BGRA in memory is what a little-endian ARGB32 is.
    outcome.guest = QImage(pixels->bgra.data(), static_cast<int>(pixels->width),
                           static_cast<int>(pixels->height), static_cast<int>(pixels->width * 4),
                           QImage::Format_ARGB32)
                        .convertToFormat(QImage::Format_RGB32);
    lay_host_scene(outcome);

    QRegion everything;
    const QRect bounds(QPoint(0, 0), scene.size());
    for (ScenarioOutcome::Window& window : outcome.windows) {
        window.casts_shadow.reset();
        window.pixels.reset();
        if (!on_screen(*window.truth)) continue;
        const QRect visible = rect_of(window.truth->visible);
        if (!window.truth->zoomed) window.casts_shadow = shadow_around(outcome, visible);
        const QRegion region = QRegion{visible.translated(-scene.topLeft()) & bounds}
                               - settings_.shell_overlay.translated(-scene.topLeft());
        window.pixels.emplace(outcome.guest, outcome.host, region, settings_.channel_tolerance);
        everything |= region;
    }
    outcome.scene_pixels.emplace(outcome.guest, outcome.host, everything, settings_.channel_tolerance);
}

// Bottom first, in the guest's own z-order: which window covers which is
// the guest's to say, and the guest's picture has it so.
void ScenarioRunner::lay_host_scene(ScenarioOutcome& outcome) {
    HostScene scene{outcome.scene, settings_.backdrop};
    std::vector<ScenarioOutcome::Window*> order;
    order.reserve(outcome.windows.size());
    for (ScenarioOutcome::Window& window : outcome.windows) order.push_back(&window);
    std::stable_sort(order.begin(), order.end(), [](const auto* a, const auto* b) {
        return a->truth->z_rank > b->truth->z_rank;
    });
    for (ScenarioOutcome::Window* window : order) {
        window->resampled = false;
        if (!on_screen(*window->truth)) continue;
        host::MirrorWindow* mirror = mirrors_.find(window->handle);
        if (mirror == nullptr) continue;
        window->resampled = !scene.add(*mirror, mirror->state(), rect_of(window->truth->visible));
    }
    outcome.host = scene.image().convertToFormat(QImage::Format_RGB32);
}

// A band just outside the window, less whatever other window covers it:
// if enough of it is not the backdrop, something is shading it.
std::optional<bool> ScenarioRunner::shadow_around(const ScenarioOutcome& outcome, QRect visible) const {
    QRegion covered;
    for (const ScenarioOutcome::Window& window : outcome.windows) {
        if (on_screen(*window.truth)) covered |= rect_of(window.truth->visible);
    }
    const QRect band = visible.adjusted(-kShadowBand, -kShadowBand, kShadowBand, kShadowBand)
                       & outcome.scene;
    const QRgb backdrop = settings_.backdrop.rgb();
    int counted = 0;
    int shaded = 0;
    for (int y = band.top(); y <= band.bottom(); ++y) {
        for (int x = band.left(); x <= band.right(); ++x) {
            const QPoint at(x, y);
            if (covered.contains(at)) continue;
            ++counted;
            if (delta(outcome.guest.pixel(at - outcome.scene.topLeft()), backdrop) > kShadowDelta) ++shaded;
        }
    }
    if (counted < kShadowMinimumPixels) return std::nullopt;
    return shaded >= counted * kShadowShare;
}

void ScenarioRunner::check_properties(ScenarioOutcome& outcome) {
    for (ScenarioOutcome::Window& window : outcome.windows) {
        window.checks.clear();
        const auto owner = std::find_if(outcome.windows.begin(), outcome.windows.end(),
                                        [&](const auto& other) {
                                            return window.truth->owner != 0
                                                   && other.handle == window.truth->owner;
                                        });
        const bool owner_made = owner != outcome.windows.end();
        const Expectation expectation{*window.as_made, *window.truth,
                                      owner_made ? owner->truth : std::optional<WindowTruth>{},
                                      window.casts_shadow, settings_.drop_shadows,
                                      mirrors_.guest_screen()};
        const host::MirrorWindow* mirror = mirrors_.find(window.handle);
        const auto yes_no = [](bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); };
        window.checks.push_back(Expectation::Check{
            QStringLiteral("mirrored"),
            QStringLiteral("a window Windows shows has a mirror; one it does not show has none"),
            yes_no(expectation.wants_mirror()), yes_no(mirror != nullptr),
            expectation.wants_mirror() == (mirror != nullptr)});
        if (const auto reported = reported_.constFind(window.handle); reported != reported_.cend()) {
            window.reported = *reported;
        }
        window.mirror.reset();
        if (mirror == nullptr || !window.truth->exists) continue;
        window.mirror = mirrors_.state_of(window.handle);
        std::optional<host::MirrorState> owner_state;
        if (owner_made) owner_state = mirrors_.state_of(owner->handle);
        std::vector<Expectation::Check> checks =
            expectation.against(*window.mirror, owner_state.has_value() ? &*owner_state : nullptr);
        window.checks.insert(window.checks.end(), std::make_move_iterator(checks.begin()),
                             std::make_move_iterator(checks.end()));
    }
    check_pointer(outcome);
}

void ScenarioRunner::check_pointer(ScenarioOutcome& outcome) {
    outcome.pointer.reset();
    if (!pointer_ || outcome.windows.empty()) return;
    QString error;
    std::optional<CursorTruth> truth = probe_.observe_cursor(error);
    if (!truth.has_value()) {
        outcome.problems.push_back(QStringLiteral("the cursor could not be observed: %1").arg(error));
        return;
    }
    ScenarioOutcome::Pointer pointer;
    pointer.window = std::clamp(pointer_window_, 0, static_cast<int>(outcome.windows.size()) - 1);
    const ScenarioOutcome::Window& window = outcome.windows[static_cast<std::size_t>(pointer.window)];
    const auto foreground = std::find_if(outcome.windows.begin(), outcome.windows.end(), [](const auto& candidate) {
        return candidate.truth.has_value() && candidate.truth->exists && candidate.truth->foreground;
    });
    const std::optional<host::MirrorState> pointer_mirror = mirrors_.state_of(window.handle);
    if (pointer_mirror.has_value() && window.truth.has_value()) {
        const std::optional<host::MirrorState> foreground_mirror =
            foreground == outcome.windows.end() ? std::nullopt : mirrors_.state_of(foreground->handle);
        const PointerExpectation expectation{PointerExpectation::Sides{
            *truth, *window.truth, foreground == outcome.windows.end() ? nullptr : &*foreground->truth, *pointer_mirror,
            foreground_mirror.has_value() ? &*foreground_mirror : nullptr, held_before_, let_go_,
            settings_.channel_tolerance}};
        pointer.checks = expectation.checks();
        pointer.wanted = expectation.wanted();
        if (truth->shown() && !truth->frames.empty()) {
            const CursorPictureOnGlass guest = CursorPictureOnGlass::of_guest(truth->frames.front(), true);
            pointer.guest = guest.image();
            if (const auto host = CursorPictureOnGlass::of_mirror(*pointer_mirror, true)) {
                pointer.guest = guest.aligned_with(*host);
                pointer.host = host->aligned_with(guest);
            }
        }
    }
    pointer.truth = std::move(truth);
    held_before_ = pointer.wanted;
    outcome.pointer = std::move(pointer);
}

void ScenarioRunner::put_input(const Scenario& scenario, ScenarioOutcome& outcome) {
    if (scenario.input.empty()) return;
    QString error;
    if (!probe_.drain_input(error).has_value()) {
        outcome.problems.push_back(QStringLiteral("the probe's input could not be read: %1").arg(error));
        return;
    }
    for (const Scenario::Step& step : scenario.input) put_step(step, outcome, outcome.input);
}

void ScenarioRunner::put_step(const Scenario::Step& step, ScenarioOutcome& outcome,
                              std::vector<ScenarioOutcome::Input>& checks) {
    const QString label = step_label(step);
    const ScenarioOutcome::Window& window = outcome.windows[static_cast<std::size_t>(step.window)];
    host::MirrorWindow* mirror = mirrors_.find(window.handle);
    if (mirror == nullptr || mirror->windowHandle() == nullptr) {
        checks.push_back({label, {QStringLiteral("a mirror to put input into"), QStringLiteral("none"), false}});
        return;
    }
    if (step.kind != Scenario::Step::Kind::Key) pointer_window_ = step.window;
    if (step.kind == Scenario::Step::Kind::Relative) {
        put_motion(step, window, checks);
        return;
    }
    if (step.kind == Scenario::Step::Kind::Resize) {
        checks.push_back({label, resize(step, window, *mirror)});
        // The guest's window is that size; the frames on the way to it are
        // still arriving, each drawn where its size puts it, and a click
        // aimed now would be measured against one of them.
        if (!settle(outcome)) outcome.problems.push_back(QStringLiteral("the mirrors did not settle after a resize"));
        return;
    }
    QString error;
    const std::optional<WindowTruth> before = probe_.observe(window.handle, error);
    const std::optional<host::MirrorState> forward_before = brought_forward(before);
    deliver(step, *window.truth, *mirror);
    const InputExpectation expectation{step, *window.truth};
    const std::optional<std::vector<InputRecord>> given =
        collect([&](const std::vector<InputRecord>& records) { return expectation.satisfied_by(records); }, error);
    if (!given.has_value()) {
        outcome.problems.push_back(QStringLiteral("the probe's input could not be read: %1").arg(error));
        return;
    }
    for (InputExpectation::Check& check : expectation.against(*given)) {
        checks.push_back({label, std::move(check)});
    }
    if (step.kind == Scenario::Step::Kind::Click && before.has_value() && before->enabled_popup != 0) {
        for (const InputRecord& record : *given) {
            if (record.window == before->enabled_popup) {
                RWIN_INFO("conform", "    popup {:#x} given {} wparam {:#x}", record.window,
                          InputExpectation::describe(record).toStdString(), record.wparam);
            }
        }
    }
    if (step.kind != Scenario::Step::Kind::Click || !before.has_value()) return;
    if (forward_before.has_value()) {
        checks.push_back({label, forward_check(*forward_before, *before, outcome)});
    }
    const std::optional<WindowTruth> after = probe_.observe(window.handle, error);
    if (!after.has_value()) {
        outcome.problems.push_back(QStringLiteral("w%1 could not be observed after a click: %2").arg(step.window).arg(error));
        return;
    }
    checks.push_back({label, activation(*before, *after)});
}

void ScenarioRunner::run_steps(const Scenario& scenario, ScenarioOutcome& outcome) {
    if (scenario.steps.empty()) return;
    const ScenarioOutcome::Stage first = snapshot(outcome);
    for (const Scenario::Step& step : scenario.steps) {
        ScenarioOutcome::Stage stage;
        stage.step = step_label(step);
        RWIN_INFO("conform", "  step {}", stage.step.toStdString());
        take_step(step, outcome, stage);
        look_again(outcome, stage.problems);
        ScenarioOutcome::Stage seen = snapshot(outcome);
        stage.windows = std::move(seen.windows);
        stage.scene = seen.scene;
        stage.guest = std::move(seen.guest);
        stage.host = std::move(seen.host);
        stage.scene_pixels = std::move(seen.scene_pixels);
        stage.pointer = std::move(seen.pointer);
        outcome.stages.push_back(std::move(stage));
    }
    // The scenario as first looked at is the outcome's own; the steps are
    // its stages.
    restore(outcome, first);
}

void ScenarioRunner::take_step(const Scenario::Step& step, ScenarioOutcome& outcome,
                               ScenarioOutcome::Stage& stage) {
    switch (step.kind) {
        case Scenario::Step::Kind::Act:
            act(step, outcome, stage);
            return;
        case Scenario::Step::Kind::HostMove:
            stage.input.push_back({step_label(step), move_on_host(step, outcome)});
            return;
        case Scenario::Step::Kind::HostUnfocus:
        case Scenario::Step::Kind::HostFocus:
        case Scenario::Step::Kind::HostEscape:
        case Scenario::Step::Kind::HostRegrab:
            as_user_here(step, outcome, stage);
            return;
        default:
            break;
    }
    QString error;
    if (!probe_.drain_input(error).has_value()) {
        stage.problems.push_back(QStringLiteral("the probe's input could not be read: %1").arg(error));
        return;
    }
    put_step(step, outcome, stage.input);
}

void ScenarioRunner::act(const Scenario::Step& step, ScenarioOutcome& outcome, ScenarioOutcome::Stage& stage) {
    const auto handle_of = [&](int index) {
        return index >= 0 ? outcome.windows[static_cast<std::size_t>(index)].handle : std::uint64_t{0};
    };
    ProbeAction action = step.action;
    action.window = handle_of(step.window);
    action.other = handle_of(step.other);
    QString error;
    const std::optional<std::uint64_t> made = probe_.act(action, error);
    if (!made.has_value()) {
        stage.problems.push_back(QStringLiteral("%1 failed: %2").arg(step_label(step), error));
        return;
    }
    if (action.kind == ProbeAction::Kind::Click || action.kind == ProbeAction::Kind::GuestMove
        || action.kind == ProbeAction::Kind::SetCursorPos) {
        pointer_window_ = step.window;
    }
    if (action.kind == ProbeAction::Kind::Click) note_guest_click(outcome, stage);
    if (!action.makes_window()) return;
    ScenarioOutcome::Window window;
    window.index = static_cast<int>(outcome.windows.size());
    window.title = QString::fromStdString(action.title);
    window.handle = *made;
    outcome.windows.push_back(std::move(window));
}

// What the step left the windows as, before the mirror reacted to it, is
// what they were made as for this stage: the mirror must not disturb them
// from there.
// The mirror put somewhere else on the desktop by the compositor - as a
// drag that has ended, or a placement of its own, leaves it: through
// KWin's scripting, by a script of the harness's own rather than through
// the mirror. The guest window must then be where the mirror is: its
// picture's corner at the guest pixel under the mirror's.
InputExpectation::Check ScenarioRunner::move_on_host(const Scenario::Step& step, const ScenarioOutcome& outcome) {
    const ScenarioOutcome::Window& window = outcome.windows[static_cast<std::size_t>(step.window)];
    const QString expected = QStringLiteral("guest window at %1,%2").arg(step.to.x()).arg(step.to.y());
    const std::optional<host::ScreenMapping> screen = mirrors_.guest_screen();
    if (!screen.has_value()) {
        return InputExpectation::Check{expected, QStringLiteral("not moved: this desktop is not KWin's"), false};
    }
    const QPointF to = screen->host_point(step.to);
    const QString source = QStringLiteral(
        "for (const w of workspace.windowList()) { if ((w.captionNormal || '').indexOf('%1') < 0) continue;"
        " const g = w.frameGeometry; w.frameGeometry = {x: %2, y: %3, width: g.width, height: g.height}; }")
        .arg(host::KWinPlacement::tag_for(window.handle), QString::number(to.x(), 'g', 17),
             QString::number(to.y(), 'g', 17));
    const host::KWinScript script{QStringLiteral("remotewin-conform-move-%1").arg(++timestamp_), source};
    if (!script.running()) return InputExpectation::Check{expected, script.error(), false};
    std::optional<WindowTruth> seen;
    QString error;
    static_cast<void>(wait_until(
        [&] {
            seen = probe_.observe(window.handle, error);
            return seen.has_value() && QPoint(seen->visible.left, seen->visible.top) == step.to;
        },
        kInputTimeout));
    const QString actual = seen.has_value()
                               ? QStringLiteral("guest window at %1,%2").arg(seen->visible.left).arg(seen->visible.top)
                               : error;
    return InputExpectation::Check{expected, actual, expected == actual};
}

// What the user does to the held pointer on this desktop, through Qt's
// window-system interface as the platform plugin reports it: the focus
// moving (wl_keyboard leave and enter), a key chord, a click.
void ScenarioRunner::as_user_here(const Scenario::Step& step, ScenarioOutcome& outcome, ScenarioOutcome::Stage& stage) {
    const ScenarioOutcome::Window& window = outcome.windows[static_cast<std::size_t>(step.window)];
    host::MirrorWindow* mirror = mirrors_.find(window.handle);
    QWindow* surface = mirror == nullptr ? nullptr : mirror->windowHandle();
    if (surface == nullptr) {
        stage.problems.push_back(QStringLiteral("%1: w%2 has no mirror").arg(step_label(step)).arg(step.window));
        return;
    }
    pointer_window_ = step.window;
    switch (step.kind) {
        case Scenario::Step::Kind::HostUnfocus:
            QWindowSystemInterface::handleFocusWindowChanged(nullptr, Qt::ActiveWindowFocusReason);
            let_go_ = QStringLiteral("the mirror lost the focus");
            break;
        case Scenario::Step::Kind::HostFocus:
            QWindowSystemInterface::handleFocusWindowChanged(surface, Qt::ActiveWindowFocusReason);
            let_go_.clear();
            break;
        case Scenario::Step::Kind::HostEscape: {
            const host::KeyTranslator keys{QGuiApplication::platformName()};
            for (const QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
                QWindowSystemInterface::handleExtendedKeyEvent(surface, type, Qt::Key_G, Qt::ControlModifier | Qt::AltModifier,
                                                               keys.native_scan_code(KEY_G), 0, 0, QStringLiteral("g"));
            }
            let_go_ = QStringLiteral("the user pressed Ctrl+Alt+G");
            break;
        }
        case Scenario::Step::Kind::HostRegrab: {
            const QPointF middle(surface->width() / 2.0, surface->height() / 2.0);
            qt_handleMouseEvent(surface, middle, surface->mapToGlobal(middle), Qt::LeftButton, Qt::LeftButton,
                                QEvent::MouseButtonPress, Qt::NoModifier, ++timestamp_);
            qt_handleMouseEvent(surface, middle, surface->mapToGlobal(middle), Qt::NoButton, Qt::LeftButton,
                                QEvent::MouseButtonRelease, Qt::NoModifier, ++timestamp_);
            let_go_.clear();
            break;
        }
        default:
            return;
    }
    QWindowSystemInterface::flushWindowSystemEvents();
    stage.notes.push_back(step_label(step));
}

// A click Windows was given itself, not through a mirror: which windows it
// activated and how often each caption was toggled - FlashWindowEx is a
// caption drawn active and inactive in turn.
void ScenarioRunner::note_guest_click(const ScenarioOutcome& outcome, ScenarioOutcome::Stage& stage) {
    idle_for(kFlashTime);
    QString error;
    const std::optional<std::vector<InputRecord>> given = probe_.drain_input(error);
    if (!given.has_value()) {
        stage.problems.push_back(QStringLiteral("the probe's input could not be read: %1").arg(error));
        return;
    }
    for (const ScenarioOutcome::Window& window : outcome.windows) {
        int toggles = 0;
        int activations = 0;
        int buttons = 0;
        for (const InputRecord& record : *given) {
            if (record.window != window.handle) continue;
            toggles += record.message == kWmNcActivate ? 1 : 0;
            activations += record.message == kWmActivate && record.wparam != 0 ? 1 : 0;
            buttons += record.message == kWmLButtonDown ? 1 : 0;
        }
        if (toggles + activations + buttons == 0) continue;
        stage.notes.push_back(QStringLiteral("w%1 given %2 WM_NCACTIVATE, %3 activations, %4 WM_LBUTTONDOWN")
                                  .arg(window.index).arg(toggles).arg(activations).arg(buttons));
    }
}

void ScenarioRunner::look_again(ScenarioOutcome& outcome, QStringList& problems) {
    if (!read_truths(outcome)) return;
    for (ScenarioOutcome::Window& window : outcome.windows) window.as_made = window.truth;
    if (!settle(outcome)) problems.push_back(QStringLiteral("the mirrors did not settle"));
    if (!read_truths(outcome)) return;
    for (int attempt = 1; attempt <= kMaxLooks; ++attempt) {
        const QList<qint64> before = signature(outcome);
        look(outcome);
        idle_for(kRecheck);
        if (signature(outcome) == before) break;
        if (attempt == kMaxLooks) problems.push_back(QStringLiteral("the mirrors kept changing while looked at"));
        static_cast<void>(settle(outcome));
        static_cast<void>(read_truths(outcome));
    }
    check_properties(outcome);
}

ScenarioOutcome::Stage ScenarioRunner::snapshot(const ScenarioOutcome& outcome) {
    ScenarioOutcome::Stage stage;
    stage.windows = outcome.windows;
    stage.scene = outcome.scene;
    stage.guest = outcome.guest;
    stage.host = outcome.host;
    stage.scene_pixels = outcome.scene_pixels;
    stage.pointer = outcome.pointer;
    return stage;
}

void ScenarioRunner::restore(ScenarioOutcome& outcome, const ScenarioOutcome::Stage& stage) {
    // The windows steps made are kept, as they were first seen: in a
    // later stage, not this one.
    for (std::size_t i = 0; i < stage.windows.size(); ++i) outcome.windows[i] = stage.windows[i];
    outcome.windows.resize(stage.windows.size());
    outcome.scene = stage.scene;
    outcome.guest = stage.guest;
    outcome.host = stage.host;
    outcome.scene_pixels = stage.scene_pixels;
    outcome.pointer = stage.pointer;
}

// Clicked while disabled with an enabled window of its own up - its modal
// dialog - Windows activates that window and flashes it (GW_ENABLEDPOPUP;
// measured: the dialog's caption toggles some fifteen times) and the click
// reaches nothing. The dialog's mirror as it was before the click, when
// that is the case.
std::optional<host::MirrorState> ScenarioRunner::brought_forward(const std::optional<WindowTruth>& clicked) const {
    if (!clicked.has_value() || clicked->enabled || clicked->enabled_popup == 0) return std::nullopt;
    const host::MirrorWindow* popup = mirrors_.find(clicked->enabled_popup);
    if (popup == nullptr) return std::nullopt;
    return popup->state();
}

InputExpectation::Check ScenarioRunner::forward_check(const host::MirrorState& before, const WindowTruth& clicked,
                                                      const ScenarioOutcome& outcome) const {
    const host::MirrorWindow* popup = mirrors_.find(clicked.enabled_popup);
    const auto index = std::find_if(outcome.windows.begin(), outcome.windows.end(),
                                    [&](const auto& window) { return window.handle == clicked.enabled_popup; });
    const QString name = index == outcome.windows.end() ? QStringLiteral("its enabled popup")
                                                        : QStringLiteral("w%1").arg(index->index);
    const QString expected = QStringLiteral("%1 brought forward: asked to be active, alerted").arg(name);
    if (popup == nullptr) return InputExpectation::Check{expected, QStringLiteral("%1 has no mirror").arg(name), false};
    const host::MirrorState after = popup->state();
    const bool asked = after.activation_requests > before.activation_requests || after.active;
    const bool alerted = after.alerts > before.alerts;
    QStringList done;
    done << (asked ? QStringLiteral("asked to be active") : QStringLiteral("not asked to be active"))
         << (alerted ? QStringLiteral("alerted") : QStringLiteral("not alerted"));
    return InputExpectation::Check{expected, QStringLiteral("%1: %2").arg(name, done.join(QStringLiteral(", "))),
                                   asked && alerted};
}

// What a click does to the foreground. Windows activates the top-level
// window clicked (WM_MOUSEACTIVATE) - unless it is WS_EX_NOACTIVATE, which
// "does not become the foreground window when the user clicks it", or
// disabled, or a layered WS_EX_TRANSPARENT window the click passes through;
// those leave the foreground where it was.
InputExpectation::Check ScenarioRunner::activation(const WindowTruth& before, const WindowTruth& after) {
    const bool passes_through = (before.extended_style & kWsExLayeredTransparent) == kWsExLayeredTransparent;
    const bool activates = before.enabled && !passes_through
                           && (before.extended_style & host::Win32Style::kWsExNoActivate) == 0;
    const auto text = [](bool foreground) {
        return foreground ? QStringLiteral("foreground") : QStringLiteral("not foreground");
    };
    const QString expected = text(activates || before.foreground);
    const QString actual = text(after.foreground);
    return InputExpectation::Check{QStringLiteral("%1 after the click").arg(expected),
                                   QStringLiteral("%1 after the click").arg(actual), expected == actual};
}

// The mirror sized as the compositor sizes it when its edge is dragged,
// and the guest window's visible frame then: the size the mirror holds
// whole (PixelMapping::guest_size), with the guest's invisible border
// around it in the window rectangle rather than in the picture.
InputExpectation::Check ScenarioRunner::resize(const Scenario::Step& step,
                                               const ScenarioOutcome::Window& window,
                                               host::MirrorWindow& mirror) {
    const host::PixelMapping mapping{mirror.state().device_pixel_ratio};
    const QSize logical = mapping.logical_size(step.size);
    const QSize wanted = mapping.guest_size(logical);
    mirror.resize(logical);
    std::optional<WindowTruth> seen;
    QString error;
    static_cast<void>(wait_until(
        [&] {
            seen = probe_.observe(window.handle, error);
            return seen.has_value() && QSize(seen->visible.width(), seen->visible.height()) == wanted;
        },
        kInputTimeout));
    const QString actual = seen.has_value()
                               ? QStringLiteral("%1x%2 visible").arg(seen->visible.width()).arg(seen->visible.height())
                               : error;
    const QString expected = QStringLiteral("%1x%2 visible").arg(wanted.width()).arg(wanted.height());
    return InputExpectation::Check{expected, actual, expected == actual};
}

// Delivered the way the platform delivers it, through Qt's window system
// interface into the mirror's window: the same path a pointer on this
// desktop takes from the compositor onwards.
void ScenarioRunner::deliver(const Scenario::Step& step, const WindowTruth& truth,
                             host::MirrorWindow& mirror) {
    QWindow* surface = mirror.windowHandle();
    if (step.kind == Scenario::Step::Kind::Key) {
        press_keys(step, surface);
        return;
    }
    // The centre of the guest pixel under a client point, where the mirror
    // draws it.
    const host::MirrorState state = mirror.state();
    // A WS_EX_LAYOUTRTL window counts its client x from the right, and
    // from its right edge itself: client x is that edge less the screen x.
    const bool mirrored = (truth.extended_style & kWsExLayoutRtl) != 0;
    const auto local_of = [&](QPoint client) {
        const int screen_x = mirrored ? truth.client.right - client.x() : truth.client.left + client.x();
        const QPointF pixel(screen_x - truth.visible.left + 0.5, truth.client.top + client.y() - truth.visible.top + 0.5);
        return (QPointF(state.picture.topLeft()) + pixel * state.fit) / state.device_pixel_ratio;
    };
    const QPointF local = local_of(step.client);
    const QPointF global = surface->mapToGlobal(local);
    // From the next pixel along: a pointer already on the point - left
    // there by an earlier scenario - would not move, and Windows would
    // rightly say nothing.
    if (step.kind == Scenario::Step::Kind::Move) {
        const QPointF beside = local + QPointF(state.fit, 0) / state.device_pixel_ratio;
        qt_handleMouseEvent(surface, beside, surface->mapToGlobal(beside), Qt::NoButton, Qt::NoButton,
                            QEvent::MouseMove, Qt::NoModifier, ++timestamp_);
        idle_for(kButtonGap);
    }
    qt_handleMouseEvent(surface, local, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove,
                        Qt::NoModifier, ++timestamp_);
    for (const QPoint& then : step.then) {
        idle_for(kButtonGap);
        const QPointF at = local_of(then);
        qt_handleMouseEvent(surface, at, surface->mapToGlobal(at), Qt::NoButton, Qt::NoButton, QEvent::MouseMove,
                            Qt::NoModifier, ++timestamp_);
    }
    if (step.kind != Scenario::Step::Kind::Click) return;
    idle_for(kButtonGap);
    qt_handleMouseEvent(surface, local, global, Qt::LeftButton, Qt::LeftButton,
                        QEvent::MouseButtonPress, Qt::NoModifier, ++timestamp_);
    idle_for(kButtonGap);
    qt_handleMouseEvent(surface, local, global, Qt::NoButton, Qt::LeftButton,
                        QEvent::MouseButtonRelease, Qt::NoModifier, ++timestamp_);
}

// As the platform plugin hands Qt a key from the compositor: with the
// keycode the platform reports for it, which is what tells the mirror
// which key it is, and the text it types.
void ScenarioRunner::press_keys(const Scenario::Step& step, QWindow* surface) {
    const host::KeyTranslator keys{QGuiApplication::platformName()};
    Qt::KeyboardModifiers held;
    const auto send = [&](const Scenario::Key& key, QEvent::Type type, const QString& text) {
        QWindowSystemInterface::handleExtendedKeyEvent(surface, type, key.qt_key, held,
                                                       keys.native_scan_code(key.evdev), 0, 0,
                                                       text);
        QWindowSystemInterface::flushWindowSystemEvents();
        idle_for(kKeyGap);
    };
    for (const Scenario::Key& key : step.held) {
        send(key, QEvent::KeyPress, QString());
        held |= key.qt_key == Qt::Key_Control ? Qt::ControlModifier : Qt::ShiftModifier;
    }
    send(step.key, QEvent::KeyPress, step.text);
    send(step.key, QEvent::KeyRelease, step.text);
    for (auto key = step.held.rbegin(); key != step.held.rend(); ++key) {
        held &= ~(key->qt_key == Qt::Key_Control ? Qt::ControlModifier : Qt::ShiftModifier);
        send(*key, QEvent::KeyRelease, QString());
    }
}

template <typename Satisfied>
std::optional<std::vector<InputRecord>> ScenarioRunner::collect(Satisfied satisfied, QString& error) {
    std::vector<InputRecord> given;
    QElapsedTimer clock;
    clock.start();
    while (!clock.hasExpired(kInputTimeout.count())) {
        idle_for(kInputPoll);
        std::optional<std::vector<InputRecord>> more = probe_.drain_input(error);
        if (!more.has_value()) return std::nullopt;
        given.insert(given.end(), more->begin(), more->end());
        if (satisfied(given)) break;
    }
    return given;
}

// The held pointer's motion, fed where the relative pointer protocol feeds
// it (PointerGrabber::take_motion): no client can make the compositor move
// a pointer. What the mirror passed on, and what the guest window was given.
void ScenarioRunner::put_motion(const Scenario::Step& step, const ScenarioOutcome::Window& window,
                                std::vector<ScenarioOutcome::Input>& checks) {
    const QString label = step_label(step);
    QString error;
    const std::optional<CursorTruth> before = probe_.observe_cursor(error);
    if (!before.has_value() || !window.truth.has_value()) {
        checks.push_back({label, {QStringLiteral("the cursor before the motion"), error, false}});
        return;
    }
    const auto moved = [&] {
        const std::optional<host::MirrorState> state = mirrors_.state_of(window.handle);
        return state.has_value() && state->pointer_hold.has_value() ? state->pointer_hold->moved : QPoint();
    };
    const QPoint moved_before = moved();
    for (int part = 0; part < kMotionParts; ++part) {
        mirrors_.pointer_grabber().take_motion(QPointF(step.motion) / kMotionParts);
        idle_for(kButtonGap);
    }
    const QPoint passed_on = moved() - moved_before;
    checks.push_back({label, {QStringLiteral("the mirror passes on %1,%2").arg(step.motion.x()).arg(step.motion.y()),
                              QStringLiteral("passed on %1,%2").arg(passed_on.x()).arg(passed_on.y()),
                              passed_on == step.motion}});
    const MotionExpectation expectation{step.motion, *window.truth, *before};
    const std::optional<std::vector<InputRecord>> given =
        collect([&](const std::vector<InputRecord>& records) { return expectation.satisfied_by(records); }, error);
    if (!given.has_value()) {
        checks.push_back({label, {QStringLiteral("the probe's input"), error, false}});
        return;
    }
    for (InputExpectation::Check& check : expectation.against(*given)) checks.push_back({label, std::move(check)});
}

void ScenarioRunner::tear_down(ScenarioOutcome& outcome) {
    QString error;
    if (!probe_.destroy_all(error)) {
        outcome.problems.push_back(QStringLiteral("the probe's windows could not be destroyed: %1").arg(error));
        return;
    }
    const bool gone = wait_until(
        [&] {
            return std::none_of(outcome.windows.begin(), outcome.windows.end(),
                                [&](const auto& window) { return mirrors_.find(window.handle) != nullptr; });
        },
        kTearDownTimeout);
    if (!gone) {
        outcome.problems.push_back(QStringLiteral("a mirror was still up %1 s after its window was destroyed")
                                       .arg(kTearDownTimeout.count() / 1000));
    }
}

}  // namespace rwin::conform
