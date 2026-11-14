#include "host/dbus/bus_connection.hpp"
#include "host/link/guest_window.hpp"
#include "host/loop/poll_loop.hpp"
#include "host/placement/kwin_placement.hpp"
#include "host/placement/placement_sync.hpp"

#include "fake_kwin_bus.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {

using rwin::Rect;
using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::BusConnection;
using rwin::host::GuestWindow;
using rwin::host::KWinPlacement;
using rwin::host::PlacementSync;
using rwin::host::Point;
using rwin::host::PointF;
using rwin::host::PollLoop;
using rwin::test::FakeKWinBus;

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

bool run_until(PollLoop& loop, const std::function<bool()>& reached) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const auto look = loop.timer([&] {
        if (reached() || std::chrono::steady_clock::now() > deadline) loop.quit(0);
    });
    look->start(std::chrono::milliseconds(1), true);
    static_cast<void>(loop.run());
    return reached();
}

void turn(PollLoop& loop) {
    const auto stop = loop.timer([&] { loop.quit(0); });
    stop->start(std::chrono::milliseconds::zero(), false);
    static_cast<void>(loop.run());
}

constexpr std::uint64_t kWindow = 0x000302ce00000001;

struct Placed {
    std::uint64_t window;
    PointF corner;
};

struct Placing {
    FakeKWinBus kwin;
    PollLoop loop;
    BusConnection bus{loop};
    KWinPlacement placement{loop, bus};
    std::vector<Placed> placed;

    Placing() {
        placement.window_placed.connect(
            [this](std::uint64_t window, PointF corner, std::int64_t) { placed.push_back(Placed{window, corner}); });
        check(run_until(loop, [this] { return placement.active(); }), "the watcher runs in the test's KWin");
    }

    [[nodiscard]] bool ordered(int orders) {
        return run_until(loop, [&] { return kwin.orders() == orders; });
    }

    void turns(int count) {
        for (int at = 0; at < count; ++at) {
            turn(loop);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

void positions_wanted_while_one_is_outstanding_leave_only_the_last() {
    Placing placing;
    placing.kwin.hold_reports(true);
    placing.placement.place(kWindow, PointF{100, 100});
    check(placing.ordered(1), "the first position goes to KWin");
    placing.placement.place(kWindow, PointF{200, 200});
    placing.placement.place(kWindow, PointF{300, 300});
    placing.placement.place(kWindow, PointF{400, 400});
    placing.turns(20);
    check(placing.kwin.orders() == 1, "nothing more goes while KWin has not reported the first");
    placing.kwin.release();
    check(placing.ordered(2), "its report sends one more");
    check(placing.kwin.where(KWinPlacement::tag_for(kWindow)) == "400 400", "the last position wanted");
    check(placing.placed.empty(), "a placement followed by another is not reported as the window's");
    placing.kwin.release();
    check(run_until(placing.loop, [&] { return !placing.placed.empty(); }), "the last placement is reported");
    check(placing.placed.size() == 1 && placing.placed[0].window == kWindow && placing.placed[0].corner.x == 400
              && placing.placed[0].corner.y == 400,
          "once, where the last position was");
    check(placing.kwin.loads() == 1, "with no script but the watcher loaded");
}

void a_report_of_a_placement_since_replaced_is_not_taken() {
    Placing placing;
    placing.kwin.hold_reports(true);
    placing.placement.place(kWindow, PointF{100, 100});
    check(placing.ordered(1), "the first position goes to KWin");
    placing.placement.place(kWindow, PointF{200, 200});
    placing.kwin.release();
    check(placing.ordered(2), "the second follows the first's report");
    placing.kwin.report_again(0);
    placing.turns(20);
    check(placing.placed.empty(), "the first's report said again ends nothing");
    placing.placement.place(kWindow, PointF{300, 300});
    placing.kwin.release();
    check(placing.ordered(3), "the second's own report sends the third");
    placing.kwin.release();
    check(run_until(placing.loop, [&] { return !placing.placed.empty(); }), "the third is reported");
    check(placing.placed.size() == 1 && placing.placed[0].corner.x == 300, "and is the only one taken");
}

void a_script_left_waiting_is_answered_before_kwin_gives_up() {
    Placing placing;
    check(run_until(placing.loop, [&] { return placing.kwin.idle_answers() > 0; }),
          "a script with nothing to do is answered so that it asks again");
    placing.placement.place(kWindow, PointF{100, 100});
    check(run_until(placing.loop, [&] { return !placing.placed.empty(); }), "and a placement after it still lands");
}

GuestWindow guest_at(WindowId window, int x, int y) {
    WindowFields fields;
    fields.geometry.frame = Rect{x, y, 800, 600};
    fields.geometry.picture = Rect{0, 0, 800, 600};
    return GuestWindow{window, fields};
}

void a_stream_of_guest_moves() {
    FakeKWinBus kwin;
    PollLoop loop;
    BusConnection bus{loop};
    PlacementSync sync{loop, bus};
    sync.prefer_output("A");
    sync.move_guest = [](WindowId, Point) { return true; };
    sync.compositor_has_state = [](WindowId) { return false; };
    check(run_until(loop, [&] { return sync.active(); }), "the watcher runs in the test's KWin");
    constexpr WindowId kDragged{0x302ce, 1};
    constexpr WindowId kBeside{0x40111, 1};
    constexpr WindowId kPassing{0x50222, 1};
    constexpr int kWindows = 3;
    constexpr int kMoves = 4000;
    constexpr int kAppears = 700;
    constexpr int kDisappears = 2600;
    bool orders_outran_reports = false;
    for (int move = 0; move < kMoves; ++move) {
        sync.follow_guest(guest_at(kDragged, 100 + move % 900, 80 + move % 400), 1, true, false);
        if (move % 5 == 0) sync.follow_guest(guest_at(kBeside, 300 + move % 200, 200), 1, true, false);
        if (move >= kAppears && move < kDisappears) {
            sync.follow_guest(guest_at(kPassing, 20 + move % 50, 20 + move % 70), 1 + move / 500, move % 11 != 0, false);
        }
        if (move == kDisappears) sync.forget(kPassing);
        if (move % 97 == 0) sync.hold_for_drag(kBeside);
        if (move % 97 == 40) sync.end_drag_hold(kBeside);
        if (move % 3 == 0) turn(loop);
        if (kwin.orders() > kwin.placed_reports() + kWindows) orders_outran_reports = true;
    }
    const std::string last = std::to_string(100 + (kMoves - 1) % 900) + " " + std::to_string(80 + (kMoves - 1) % 400);
    check(run_until(loop, [&] { return kwin.where(KWinPlacement::tag_for(kDragged.key())) == last; }),
          "the window ends where the guest last had it");
    check(run_until(loop, [&] { return kwin.placed_reports() == kwin.orders(); }), "every placement is reported");
    check(kwin.loads() == 1, "no script is loaded for a placement");
    check(kwin.orders() > 0 && kwin.orders() < kMoves, "fewer placements go to KWin than the guest made moves");
    check(!orders_outran_reports, "a window never has two placements with KWin at once");
}

}

int main() {
    positions_wanted_while_one_is_outstanding_leave_only_the_last();
    a_report_of_a_placement_since_replaced_is_not_taken();
    a_stream_of_guest_moves();
    a_script_left_waiting_is_answered_before_kwin_gives_up();
    if (failures == 0) std::printf("kwin_placement: all passed\n");
    return failures == 0 ? 0 : 1;
}
