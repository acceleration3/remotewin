// Resizing, maximising and minimising a mirror, end to end: a headless KWin,
// the remotewin host under test, and a played guest (TestDesktop). Each case
// is one ctest test in the "integration" label; a case runs in a D-Bus
// session of its own, which it starts by running itself again under
// dbus-run-session.
//
//   rwin_resize_integration <case>      one case
//   rwin_resize_integration             the list
//
// RWIN_INTEGRATION_HOST runs another remotewin than the one built with it;
// RWIN_INTEGRATION_HOST_LOG sets its log level (debug), and
// RWIN_INTEGRATION_WAYLAND_DEBUG=1 adds its Wayland traffic to its log.
//
// Exit 0 passed, 1 failed, 77 skipped (no kwin_wayland or dbus-run-session).

#include "test_desktop.hpp"

#include "host/latency/frame_stamps.hpp"
#include "host/geometry/size.hpp"
#include "host/geometry/view_mapping.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#ifndef RWIN_HOST_BINARY
#error "RWIN_HOST_BINARY names the remotewin under test"
#endif

namespace rwin::integration {

namespace {

using std::chrono::milliseconds;
using Command = SysCommand::Command;
using host::Size;
using host::ViewMapping;

constexpr int kSkipped = 77;
constexpr const char* kInPrivateBus = "RWIN_INTEGRATION_BUS";
// How long a state change may take to settle, and how long it has to hold
// to count as settled.
constexpr milliseconds kSettleBound{1000};
constexpr milliseconds kSettleWait{3000};
constexpr milliseconds kHold{250};
// A drag's pointer steps: a pointer reports about this often.
constexpr milliseconds kDragStep{8};
constexpr double kNear = 1.01;

// One case's verdicts, said as they are reached.
class Verdicts {
public:
    void check(bool ok, const std::string& what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failed_;
    }
    void note(const std::string& what) { std::printf("  .... %s\n", what.c_str()); }
    [[nodiscard]] bool passed() const noexcept { return failed_ == 0; }

private:
    int failed_ = 0;
};

bool near(double a, double b) { return std::abs(a - b) < kNear; }

std::string box_text(const KWinView::Seen& seen) {
    return std::format("{:.1f},{:.1f} {:.1f}x{:.1f}", seen.x, seen.y, seen.width, seen.height);
}

std::string rect_text(const Rect& rect) { return std::format("{},{} {}x{}", rect.x, rect.y, rect.width, rect.height); }

double ms_since(std::int64_t from_ns, std::int64_t to_ns) { return static_cast<double>(to_ns - from_ns) / 1e6; }

// What one case has to run and judge with.
class Case {
public:
    Case(TestDesktop& desktop, Verdicts& verdicts) : desktop_(desktop), verdicts_(verdicts) {}

    TestDesktop& desktop() { return desktop_; }
    Verdicts& verdicts() { return verdicts_; }
    KWinView& kwin() { return desktop_.kwin(); }
    FakeGuest& guest() { return desktop_.guest(); }
    ModelWindow& window() { return desktop_.window(); }

    // Marks where the journals stand, so what follows is judged alone.
    void mark() {
        kwin_mark_ = kwin().journal().size();
        asked_mark_ = guest().asked().size();
        placed_mark_ = guest().placed().size();
        pictured_mark_ = guest().pictured().size();
    }

    // The guest window's size, shown by the host: the mirror's picture.
    [[nodiscard]] bool host_shows(std::uint32_t width, std::uint32_t height) {
        const std::optional<HostReport::Picture> picture = desktop_.host_report().picture();
        return picture.has_value() && picture->width == static_cast<int>(width)
               && picture->height == static_cast<int>(height);
    }

    // How many times KWin's maximise mode or minimised flag changed since the mark.
    [[nodiscard]] int flips(const std::function<int(const KWinView::Seen&)>& state) const {
        const auto& journal = desktop_.kwin().journal();
        int flips = 0;
        for (std::size_t at = std::max<std::size_t>(kwin_mark_, 1); at < journal.size(); ++at) {
            if (state(journal[at]) != state(journal[at - 1])) ++flips;
        }
        return flips;
    }

    [[nodiscard]] int commands_asked(Command command) const {
        int count = 0;
        const auto& asked = desktop_.guest().asked();
        for (std::size_t at = asked_mark_; at < asked.size(); ++at) {
            if (const auto* asked_command = std::get_if<Command>(&asked[at].request)) count += *asked_command == command;
        }
        return count;
    }

    [[nodiscard]] std::vector<Rect> moves_asked() const {
        std::vector<Rect> moves;
        const auto& asked = desktop_.guest().asked();
        for (std::size_t at = asked_mark_; at < asked.size(); ++at) {
            if (const auto* frame = std::get_if<Rect>(&asked[at].request)) moves.push_back(*frame);
        }
        return moves;
    }

    // Frames the guest window had since the mark that were neither of these.
    [[nodiscard]] std::vector<Rect> placed_other_than(const std::vector<Rect>& expected) const {
        std::vector<Rect> others;
        const auto& placed = desktop_.guest().placed();
        for (std::size_t at = placed_mark_; at < placed.size(); ++at) {
            if (std::find(expected.begin(), expected.end(), placed[at].frame) == expected.end()) {
                others.push_back(placed[at].frame);
            }
        }
        return others;
    }

    [[nodiscard]] std::size_t kwin_mark() const noexcept { return kwin_mark_; }
    [[nodiscard]] std::size_t pictured_mark() const noexcept { return pictured_mark_; }

    void judge_settled(const std::optional<milliseconds>& took, const std::string& what) {
        verdicts_.check(took.has_value() && *took <= kSettleBound,
                        took.has_value() ? std::format("{} settled in {} ms (bound {} ms)", what, took->count(),
                                                       kSettleBound.count())
                                         : std::format("{} never settled in {} ms", what, kSettleWait.count()));
    }

    void dump_kwin_since_mark() const {
        const auto& journal = desktop_.kwin().journal();
        for (std::size_t at = kwin_mark_; at < journal.size(); ++at) {
            const KWinView::Seen& seen = journal[at];
            std::printf("        kwin %-10s %s max %d min %d resizing %d\n", seen.what.c_str(), box_text(seen).c_str(),
                        seen.maximize_mode, seen.minimized, seen.resizing);
        }
    }

private:
    TestDesktop& desktop_;
    Verdicts& verdicts_;
    std::size_t kwin_mark_ = 0;
    std::size_t asked_mark_ = 0;
    std::size_t placed_mark_ = 0;
    std::size_t pictured_mark_ = 0;
};

int maximize_mode(const KWinView::Seen& seen) { return seen.maximize_mode; }
int minimized(const KWinView::Seen& seen) { return seen.minimized ? 1 : 0; }

// The compositor maximises the mirror (its maximise button, a shortcut) and
// restores it: the guest window does the same once each, nothing flips
// back, and the restore puts both where they were.
void maximize_here(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    const Rect guest_before = run.window().frame();
    const Rect area = run.window().work_area();
    run.mark();
    run.kwin().maximize(true);
    const auto maximized = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            return now->maximize_mode == 3 && run.window().maximized() && run.host_shows(area.width, area.height);
        },
        kSettleWait, kHold);
    run.judge_settled(maximized, "maximise from the compositor");
    run.verdicts().check(run.flips(maximize_mode) == 1,
                         std::format("the compositor's maximise mode changed once ({} times)", run.flips(maximize_mode)));
    run.verdicts().check(run.commands_asked(Command::Maximize) == 1 && run.commands_asked(Command::Restore) == 0,
                         std::format("the guest was asked to maximise once ({}) and never to restore ({})",
                                     run.commands_asked(Command::Maximize), run.commands_asked(Command::Restore)));
    const std::vector<Rect> stray = run.placed_other_than({guest_before, area});
    run.verdicts().check(stray.empty(), std::format("the guest window was only where it was or maximised{}",
                                                    stray.empty() ? "" : ", not at " + rect_text(stray.front())));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();

    run.mark();
    run.kwin().maximize(false);
    const auto restored = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            return now->maximize_mode == 0 && !run.window().maximized() && run.window().frame() == guest_before
                   && near(now->x, before.x) && near(now->y, before.y) && near(now->width, before.width)
                   && near(now->height, before.height) && run.host_shows(guest_before.width, guest_before.height);
        },
        kSettleWait, kHold);
    run.judge_settled(restored, std::format("restore from the compositor to {} (guest {})", box_text(before),
                                            rect_text(guest_before)));
    if (!restored.has_value()) {
        run.verdicts().note(std::format("the compositor has it at {}, the guest at {}", box_text(*run.kwin().now()),
                                        rect_text(run.window().frame())));
    }
    run.verdicts().check(run.flips(maximize_mode) == 1,
                         std::format("the compositor's maximise mode changed once ({} times)", run.flips(maximize_mode)));
    run.verdicts().check(run.commands_asked(Command::Restore) == 1 && run.commands_asked(Command::Maximize) == 0,
                         std::format("the guest was asked to restore once ({}) and never to maximise ({})",
                                     run.commands_asked(Command::Restore), run.commands_asked(Command::Maximize)));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();
}

// The guest maximises its window itself (its own caption's button) and
// restores it: the mirror follows, and nothing is asked back of the guest.
void maximize_in_guest(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    const Rect guest_before = run.window().frame();
    const Rect area = run.window().work_area();
    run.mark();
    run.guest().act(Command::Maximize);
    const auto maximized = run.desktop().settle(
        [&] { return run.kwin().now()->maximize_mode == 3 && run.host_shows(area.width, area.height); }, kSettleWait,
        kHold);
    run.judge_settled(maximized, "the mirror maximised for the guest");
    run.verdicts().check(run.flips(maximize_mode) == 1,
                         std::format("the compositor's maximise mode changed once ({} times)", run.flips(maximize_mode)));
    run.verdicts().check(run.commands_asked(Command::Maximize) + run.commands_asked(Command::Restore) == 0
                             && run.moves_asked().empty(),
                         "nothing was asked back of the guest");
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();

    run.mark();
    run.guest().act(Command::Restore);
    const auto restored = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            return now->maximize_mode == 0 && near(now->x, before.x) && near(now->y, before.y)
                   && near(now->width, before.width) && near(now->height, before.height)
                   && run.host_shows(guest_before.width, guest_before.height);
        },
        kSettleWait, kHold);
    run.judge_settled(restored, std::format("the mirror restored for the guest to {}", box_text(before)));
    if (!restored.has_value()) run.verdicts().note(std::format("the compositor has it at {}", box_text(*run.kwin().now())));
    run.verdicts().check(run.flips(maximize_mode) == 1,
                         std::format("the compositor's maximise mode changed once ({} times)", run.flips(maximize_mode)));
    run.verdicts().check(run.commands_asked(Command::Maximize) + run.commands_asked(Command::Restore) == 0
                             && run.window().frame() == guest_before,
                         std::format("nothing was asked back of the guest, which is at {}", rect_text(run.window().frame())));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();
}

// The compositor minimises the mirror (the taskbar, a shortcut) and brings
// it back: the guest window is minimised and restored with it, once each.
void minimize_here(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    const Rect guest_before = run.window().frame();
    run.mark();
    run.kwin().minimize(true);
    const auto minimized_now = run.desktop().settle(
        [&] { return run.kwin().now()->minimized && run.window().minimized(); }, kSettleWait, kHold);
    run.judge_settled(minimized_now, "minimise from the compositor");
    run.verdicts().check(run.flips(minimized) == 1,
                         std::format("the compositor's minimised flag changed once ({} times)", run.flips(minimized)));
    run.verdicts().check(run.commands_asked(Command::Minimize) == 1 && run.commands_asked(Command::Restore) == 0,
                         std::format("the guest was asked to minimise once ({}) and never to restore ({})",
                                     run.commands_asked(Command::Minimize), run.commands_asked(Command::Restore)));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();

    run.mark();
    run.kwin().minimize(false);
    const auto back = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            return !now->minimized && !run.window().minimized() && run.window().frame() == guest_before
                   && near(now->x, before.x) && near(now->y, before.y) && near(now->width, before.width)
                   && near(now->height, before.height) && run.host_shows(guest_before.width, guest_before.height);
        },
        kSettleWait, kHold);
    run.judge_settled(back, "unminimise from the compositor");
    if (!back.has_value()) {
        run.verdicts().note(std::format("the compositor has it at {} minimised {}, the guest at {} minimised {}",
                                        box_text(*run.kwin().now()), run.kwin().now()->minimized,
                                        rect_text(run.window().frame()), run.window().minimized()));
    }
    run.verdicts().check(run.flips(minimized) == 1,
                         std::format("the compositor's minimised flag changed once ({} times)", run.flips(minimized)));
    run.verdicts().check(run.commands_asked(Command::Restore) == 1 && run.commands_asked(Command::Minimize) == 0,
                         std::format("the guest was asked to restore once ({}) and never to minimise ({})",
                                     run.commands_asked(Command::Restore), run.commands_asked(Command::Minimize)));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();
}

// The guest minimises its window itself and restores it: the mirror goes
// and comes back where it was, and nothing is asked back of the guest.
void minimize_in_guest(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    const Rect guest_before = run.window().frame();
    run.mark();
    run.guest().act(Command::Minimize);
    const auto minimized_now = run.desktop().settle([&] { return run.kwin().now()->minimized; }, kSettleWait, kHold);
    run.judge_settled(minimized_now, "the mirror minimised for the guest");
    run.verdicts().check(run.flips(minimized) == 1,
                         std::format("the compositor's minimised flag changed once ({} times)", run.flips(minimized)));
    run.verdicts().check(run.commands_asked(Command::Minimize) + run.commands_asked(Command::Restore) == 0
                             && run.moves_asked().empty(),
                         "nothing was asked back of the guest");
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();

    run.mark();
    run.guest().act(Command::Restore);
    const auto back = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            return !now->minimized && near(now->x, before.x) && near(now->y, before.y)
                   && near(now->width, before.width) && near(now->height, before.height)
                   && run.host_shows(guest_before.width, guest_before.height);
        },
        kSettleWait, kHold);
    run.judge_settled(back, "the mirror unminimised for the guest");
    if (!back.has_value()) {
        run.verdicts().note(std::format("the compositor has it at {} minimised {}", box_text(*run.kwin().now()),
                                        run.kwin().now()->minimized));
    }
    run.verdicts().check(run.commands_asked(Command::Minimize) + run.commands_asked(Command::Restore) == 0
                             && run.window().frame() == guest_before,
                         std::format("nothing was asked back of the guest, which is at {}", rect_text(run.window().frame())));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();
}

// A drag of one edge or corner, by the pointer, as the user makes it.
struct Drag {
    // Where the press is, on the mirror, in its logical units.
    double press_x = 0;
    double press_y = 0;
    // Each step's pointer motion, and how many steps.
    double step_x = 0;
    double step_y = 0;
    int steps = 0;
    // Which edges move with the pointer.
    bool moves_left = false;
    bool moves_top = false;

    // Which way the width and the height go: 1 growing, -1 shrinking, 0 not
    // at all.
    [[nodiscard]] int widening() const noexcept { return sign(moves_left ? -step_x : step_x); }
    [[nodiscard]] int heightening() const noexcept { return sign(moves_top ? -step_y : step_y); }
    // Whether ``now`` is ``want`` or past it, the way the drag goes; within
    // ``slack`` of it on a side the drag does not change.
    [[nodiscard]] bool reached(double now_width, double now_height, double want_width, double want_height,
                               double slack) const noexcept {
        return along(now_width - want_width, widening(), slack) && along(now_height - want_height, heightening(), slack);
    }
    // Whether going from one size to the next went against the drag.
    [[nodiscard]] bool against(double width, double height, double next_width, double next_height,
                               double slack) const noexcept {
        return (next_width - width) * widening() < -slack || (next_height - height) * heightening() < -slack;
    }

private:
    static int sign(double value) noexcept { return value > 0 ? 1 : (value < 0 ? -1 : 0); }
    static bool along(double difference, int way, double slack) noexcept {
        return way == 0 ? std::abs(difference) <= slack : difference * way >= -slack;
    }
};

// The mirror's frame each step should have made, in logical units.
KWinView::Seen dragged_to(const KWinView::Seen& before, const Drag& drag, int step) {
    KWinView::Seen after = before;
    const double dx = drag.step_x * step;
    const double dy = drag.step_y * step;
    if (drag.moves_left) after.x += dx;
    if (drag.moves_top) after.y += dy;
    after.width += drag.widening() * std::abs(dx);
    after.height += drag.heightening() * std::abs(dy);
    return after;
}

Size pixels_of(const KWinView::Seen& seen, double scale) {
    return ViewMapping::picture_for(
        Size{static_cast<int>(std::lround(seen.width)), static_cast<int>(std::lround(seen.height))}, scale);
}

// When each pointer step was made, and when the button came up.
struct Dragged {
    std::vector<std::int64_t> steps;
    std::int64_t released = 0;
};

std::optional<Dragged> make_drag(Case& run, const KWinView::Seen& before, const Drag& drag, const std::string& what) {
    FakePointer& pointer = run.desktop().pointer();
    pointer.move_to(before.x + drag.press_x, before.y + drag.press_y);
    run.desktop().pass(milliseconds(30));
    pointer.press();
    const std::size_t mark = run.kwin_mark();
    const auto began = run.desktop().settle(
        [&] {
            const auto& journal = run.kwin().journal();
            return std::any_of(journal.begin() + static_cast<std::ptrdiff_t>(mark), journal.end(),
                               [](const KWinView::Seen& seen) { return seen.what == "drag-start"; });
        },
        milliseconds(1000), milliseconds(0));
    run.verdicts().check(began.has_value(), std::format("the compositor began the {} resize", what));
    if (!began.has_value()) {
        pointer.release();
        return std::nullopt;
    }
    Dragged dragged;
    for (int step = 1; step <= drag.steps; ++step) {
        pointer.move_to(before.x + drag.press_x + drag.step_x * step, before.y + drag.press_y + drag.step_y * step);
        dragged.steps.push_back(host::now_ns());
        run.desktop().pass(kDragStep);
    }
    pointer.release();
    dragged.released = host::now_ns();
    return dragged;
}

void judge_drag_end(Case& run, const KWinView::Seen& end, const Dragged& dragged, const std::string& what) {
    const Size pixels = pixels_of(end, run.desktop().scale());
    const auto settled = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            const Rect frame = run.window().frame();
            return !now->resizing && near(now->x, end.x) && near(now->y, end.y) && near(now->width, end.width)
                   && near(now->height, end.height) && frame.width == static_cast<std::uint32_t>(pixels.width)
                   && frame.height == static_cast<std::uint32_t>(pixels.height)
                   && run.host_shows(frame.width, frame.height);
        },
        kSettleWait, kHold);
    run.verdicts().check(settled.has_value(), std::format("the {} drag settled at {} (guest {}x{})", what,
                                                          box_text(end), pixels.width, pixels.height));
    if (!settled.has_value()) {
        run.verdicts().note(std::format("the compositor has it at {}, the guest at {}", box_text(*run.kwin().now()),
                                        rect_text(run.window().frame())));
        return;
    }
    const double after_release = std::max(0.0, ms_since(dragged.released, host::now_ns()) - kHold.count());
    run.verdicts().check(after_release <= kSettleBound.count(),
                         std::format("settled {:.0f} ms after the release", after_release));
}

// The mirror's frame through the drag, as the compositor had it: it never
// goes back the way the drag came, and it follows each step at once.
void judge_drag_frames(Case& run, const KWinView::Seen& before, const Drag& drag, const Dragged& dragged) {
    const auto& journal = run.kwin().journal();
    std::vector<const KWinView::Seen*> frames;
    for (std::size_t at = run.kwin_mark(); at < journal.size(); ++at) {
        if (journal[at].what == "frame") frames.push_back(&journal[at]);
    }
    int went_back = 0;
    for (std::size_t at = 1; at < frames.size(); ++at) {
        if (!drag.against(frames[at - 1]->width, frames[at - 1]->height, frames[at]->width, frames[at]->height, 0.5)) {
            continue;
        }
        ++went_back;
        run.verdicts().note(std::format("the mirror went back from {} to {} at {:.0f} ms", box_text(*frames[at - 1]),
                                        box_text(*frames[at]), ms_since(dragged.steps.front(), frames[at]->at_ns)));
    }
    run.verdicts().check(went_back == 0, std::format("the mirror never went back against the drag ({} times)", went_back));
    std::vector<double> lags;
    for (int step = 1; step <= drag.steps; ++step) {
        const KWinView::Seen want = dragged_to(before, drag, step);
        const auto reached = std::find_if(frames.begin(), frames.end(), [&](const KWinView::Seen* seen) {
            return drag.reached(seen->width, seen->height, want.width, want.height, 1.0);
        });
        if (reached == frames.end()) continue;
        lags.push_back(std::max(0.0, ms_since(dragged.steps[static_cast<std::size_t>(step - 1)], (*reached)->at_ns)));
    }
    std::sort(lags.begin(), lags.end());
    const double median = lags.empty() ? -1.0 : lags[lags.size() / 2];
    const double worst = lags.empty() ? -1.0 : lags.back();
    run.verdicts().check(lags.size() == static_cast<std::size_t>(drag.steps) && median <= 40.0 && worst <= 120.0,
                         std::format("the mirror's frame followed {} of {} pointer steps, median {:.1f} ms, worst "
                                     "{:.1f} ms (bounds 40, 120)",
                                     lags.size(), drag.steps, median, worst));
}

// How long each step took to reach a picture the guest sent: said, not
// judged - it is the guest's speed as much as the host's.
void note_drag_pictures(Case& run, const KWinView::Seen& before, const Drag& drag, const Dragged& dragged) {
    const auto& pictured = run.guest().pictured();
    std::vector<double> lags;
    for (int step = 1; step <= drag.steps; ++step) {
        const std::int64_t at = dragged.steps[static_cast<std::size_t>(step - 1)];
        const Size pixels = pixels_of(dragged_to(before, drag, step), run.desktop().scale());
        const auto reached = std::find_if(pictured.begin() + static_cast<std::ptrdiff_t>(run.pictured_mark()),
                                          pictured.end(), [&](const FakeGuest::Pictured& picture) {
                                              return picture.at_ns >= at
                                                     && drag.reached(picture.width, picture.height, pixels.width,
                                                                     pixels.height, 1.0);
                                          });
        if (reached != pictured.end()) lags.push_back(ms_since(at, reached->at_ns));
    }
    std::sort(lags.begin(), lags.end());
    run.verdicts().note(std::format("the guest's pictures reached {} of {} steps, median {:.1f} ms, worst {:.1f} ms",
                                    lags.size(), drag.steps, lags.empty() ? -1.0 : lags[lags.size() / 2],
                                    lags.empty() ? -1.0 : lags.back()));
}

// What the guest was asked, in order: never back against the drag, and the
// edges the drag does not move kept where they were.
void judge_drag_asked(Case& run, const Drag& drag, const Rect& guest_before) {
    const std::vector<Rect> moves = run.moves_asked();
    int asked_back = 0;
    for (std::size_t at = 1; at < moves.size(); ++at) {
        if (!drag.against(moves[at - 1].width, moves[at - 1].height, moves[at].width, moves[at].height, 0.0)) continue;
        ++asked_back;
        run.verdicts().note(std::format("the guest was asked for {} after {}", rect_text(moves[at]),
                                        rect_text(moves[at - 1])));
    }
    run.verdicts().check(asked_back == 0, std::format("the guest was asked for {} sizes, never back against the drag "
                                                      "({} times)",
                                                      moves.size(), asked_back));
    const Rect frame = run.window().frame();
    const auto right = static_cast<std::int64_t>(frame.x) + frame.width;
    const auto bottom = static_cast<std::int64_t>(frame.y) + frame.height;
    const auto right_before = static_cast<std::int64_t>(guest_before.x) + guest_before.width;
    const auto bottom_before = static_cast<std::int64_t>(guest_before.y) + guest_before.height;
    const bool anchored = (drag.moves_left ? std::abs(right - right_before) <= 2 : frame.x == guest_before.x)
                          && (drag.moves_top ? std::abs(bottom - bottom_before) <= 2 : frame.y == guest_before.y);
    run.verdicts().check(anchored, std::format("the guest window kept its unmoved edges: {} from {}", rect_text(frame),
                                               rect_text(guest_before)));
}

// The user drags an edge: the compositor runs the resize, and the mirror
// keeps up. The end settles everywhere at the size the pointer left it.
void drag(Case& run, const Drag& drag, const std::string& what) {
    const KWinView::Seen before = *run.kwin().now();
    const Rect guest_before = run.window().frame();
    run.mark();
    const std::optional<Dragged> dragged = make_drag(run, before, drag, what);
    if (!dragged.has_value()) return;
    judge_drag_end(run, dragged_to(before, drag, drag.steps), *dragged, what);
    judge_drag_frames(run, before, drag, *dragged);
    note_drag_pictures(run, before, drag, *dragged);
    judge_drag_asked(run, drag, guest_before);
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();
}

// The bottom-right corner, in the band the host gives a window's invisible
// borders: the host asks the compositor to resize.
void drag_corner(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    drag(run, Drag{before.width - 2, before.height - 2, 8, 6, 30, false, false}, "bottom-right");
}

// The top edge, which a Windows 10 or 11 window sizes from inside its frame:
// the press goes to the guest, Windows begins its own size loop
// (MoveSizeStart), and the host hands that to the compositor.
void drag_top(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    drag(run, Drag{before.width / 2, 2, 0, -5, 30, false, true}, "top");
}

// The same corner, inwards: the window shrinks, its picture cropped until
// the guest's catches up.
void drag_corner_in(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    drag(run, Drag{before.width - 2, before.height - 2, -6, -4, 30, false, false}, "bottom-right inwards");
}

// Windows' left edge, by the band: the guest window grows to the left.
void drag_left(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    drag(run, Drag{2, before.height / 2, -6, 0, 30, true, false}, "left");
}

// The application resizes its own window (a dialog showing more, an
// installer's next page): the mirror takes the guest's size where the guest
// has the window, and the guest is not asked back to the size the
// compositor last gave.
void resize_in_guest(Case& run) {
    const Rect before = run.window().frame();
    const Rect grown{before.x, before.y, before.width + 144, before.height + 96};
    run.mark();
    run.guest().place_itself(grown);
    const double width = run.desktop().logical(static_cast<std::int32_t>(grown.width));
    const double height = run.desktop().logical(static_cast<std::int32_t>(grown.height));
    const auto settled = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            return near(now->x, run.desktop().logical(grown.x)) && near(now->y, run.desktop().logical(grown.y))
                   && near(now->width, width) && near(now->height, height) && run.window().frame() == grown
                   && run.host_shows(grown.width, grown.height);
        },
        kSettleWait, kHold);
    run.judge_settled(settled, std::format("the mirror took the guest's {}x{}", grown.width, grown.height));
    if (!settled.has_value()) {
        run.verdicts().note(std::format("the compositor has it at {}, the guest at {}", box_text(*run.kwin().now()),
                                        rect_text(run.window().frame())));
    }
    const std::vector<Rect> moves = run.moves_asked();
    run.verdicts().check(moves.empty(), std::format("the guest was asked for nothing{}",
                                                    moves.empty() ? "" : ", not " + rect_text(moves.front())));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();
}

// The compositor resizes the mirror itself, a step at a time (a script, a
// tiling shortcut): the mirror ends at the last size, and does not go back.
void resize_by_compositor(Case& run) {
    const KWinView::Seen before = *run.kwin().now();
    run.mark();
    constexpr int kSteps = 10;
    for (int step = 1; step <= kSteps; ++step) {
        run.kwin().set_frame(before.x, before.y, before.width + step * 20, before.height + step * 10);
        run.desktop().pass(milliseconds(16));
    }
    const double width = before.width + kSteps * 20;
    const double height = before.height + kSteps * 10;
    const Size pixels = ViewMapping::picture_for(
        Size{static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height))}, run.desktop().scale());
    const auto settled = run.desktop().settle(
        [&] {
            const auto& now = run.kwin().now();
            const Rect frame = run.window().frame();
            return near(now->x, before.x) && near(now->y, before.y) && near(now->width, width)
                   && near(now->height, height) && frame.width == static_cast<std::uint32_t>(pixels.width)
                   && frame.height == static_cast<std::uint32_t>(pixels.height)
                   && run.host_shows(frame.width, frame.height);
        },
        kSettleWait, kHold);
    run.judge_settled(settled, std::format("the compositor's resize to {:.0f}x{:.0f}", width, height));
    if (!settled.has_value()) {
        run.verdicts().note(std::format("the compositor has it at {}, the guest at {}", box_text(*run.kwin().now()),
                                        rect_text(run.window().frame())));
    }
    const std::vector<Rect> moves = run.moves_asked();
    int asked_back = 0;
    for (std::size_t at = 1; at < moves.size(); ++at) {
        asked_back += moves[at].width < moves[at - 1].width || moves[at].height < moves[at - 1].height;
    }
    run.verdicts().check(asked_back == 0, std::format("the guest was asked for {} sizes, never a smaller one after a "
                                                      "larger ({} times)",
                                                      moves.size(), asked_back));
    if (!run.verdicts().passed()) run.dump_kwin_since_mark();
}

struct Named {
    std::string_view name;
    void (*run)(Case&);
    FakeGuest::Timing timing;
};

// The guest's timing: an application that lays itself out in 25 ms and a
// capture a frame later; and one five times as slow.
constexpr FakeGuest::Timing kQuick{milliseconds(25), milliseconds(16)};
constexpr FakeGuest::Timing kSlow{milliseconds(120), milliseconds(33)};

constexpr Named kCases[] = {
    {"maximize-here", &maximize_here, kQuick},       {"maximize-in-guest", &maximize_in_guest, kQuick},
    {"minimize-here", &minimize_here, kQuick},       {"minimize-in-guest", &minimize_in_guest, kQuick},
    {"drag-corner", &drag_corner, kQuick},           {"drag-corner-slow-guest", &drag_corner, kSlow},
    {"drag-corner-in", &drag_corner_in, kQuick},     {"drag-top", &drag_top, kQuick},
    {"drag-left", &drag_left, kQuick},               {"resize-by-compositor", &resize_by_compositor, kQuick},
    {"resize-in-guest", &resize_in_guest, kQuick},
};

bool on_path(const std::string& program) {
    const char* path = std::getenv("PATH");
    if (path == nullptr) return false;
    std::string_view dirs{path};
    while (!dirs.empty()) {
        const std::size_t end = dirs.find(':');
        const std::filesystem::path candidate = std::filesystem::path(dirs.substr(0, end)) / program;
        if (::access(candidate.c_str(), X_OK) == 0) return true;
        if (end == std::string_view::npos) break;
        dirs.remove_prefix(end + 1);
    }
    return false;
}

int run_case(const Named& named) {
    std::string scratch = (std::filesystem::temp_directory_path() / "rwin-integration-XXXXXX").string();
    if (::mkdtemp(scratch.data()) == nullptr) {
        std::fprintf(stderr, "no scratch directory\n");
        return EXIT_FAILURE;
    }
    std::printf("%s\n", std::string(named.name).c_str());
    Verdicts verdicts;
    {
        TestDesktop::Layout layout;
        layout.timing = named.timing;
        const char* host = std::getenv("RWIN_INTEGRATION_HOST");
        TestDesktop desktop{layout, host != nullptr ? host : RWIN_HOST_BINARY, scratch};
        verdicts.check(desktop.ready(), "the mirror came up where the guest has its window");
        if (desktop.ready()) {
            Case run{desktop, verdicts};
            named.run(run);
        }
    }
    if (verdicts.passed()) {
        std::filesystem::remove_all(scratch);
        return EXIT_SUCCESS;
    }
    std::printf("  logs kept in %s\n", scratch.c_str());
    return EXIT_FAILURE;
}

}  // namespace

}  // namespace rwin::integration

int main(int argc, char** argv) {
    using namespace rwin::integration;
    if (argc < 2) {
        for (const Named& named : kCases) std::printf("%s\n", std::string(named.name).c_str());
        return EXIT_SUCCESS;
    }
    const std::string_view wanted = argv[1];
    const auto found =
        std::find_if(std::begin(kCases), std::end(kCases), [&](const Named& named) { return named.name == wanted; });
    if (found == std::end(kCases)) {
        std::fprintf(stderr, "no case '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (std::getenv(kInPrivateBus) == nullptr) {
        for (const char* needed : {"kwin_wayland", "dbus-run-session"}) {
            if (!on_path(needed)) {
                std::printf("skipped: %s is not installed\n", needed);
                return kSkipped;
            }
        }
        ::setenv(kInPrivateBus, "1", 1);
        ::execlp("dbus-run-session", "dbus-run-session", "--", argv[0], argv[1], static_cast<char*>(nullptr));
        std::perror("dbus-run-session");
        return EXIT_FAILURE;
    }
    return run_case(*found);
}
