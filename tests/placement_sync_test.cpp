#include "host/dbus/bus_connection.hpp"
#include "host/link/guest_window.hpp"
#include "host/loop/poll_loop.hpp"
#include "host/placement/kwin_placement.hpp"
#include "host/placement/output_layout.hpp"
#include "host/placement/placement_sync.hpp"

#include "fake_kwin_bus.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

using rwin::Rect;
using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::BoxF;
using rwin::host::BusConnection;
using rwin::host::GuestWindow;
using rwin::host::KWinPlacement;
using rwin::host::OutputLayout;
using rwin::host::PlacementSync;
using rwin::host::PointF;
using rwin::host::PollLoop;
using rwin::host::Point;

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

constexpr WindowId kWindow{0x1234, 1};

class FakeKWin final : public KWinPlacement {
public:
    FakeKWin(PollLoop& loop, BusConnection& bus) : KWinPlacement(loop, bus) {
        layout_.replace({OutputLayout::Output{"A", PointF{0, 0}, PointF{1920, 1080}, 1.0}});
        layout_.prefer("A");
    }

    [[nodiscard]] bool active() const noexcept override { return true; }
    [[nodiscard]] const OutputLayout& outputs() const noexcept override { return layout_; }
    void place(std::uint64_t window, PointF corner) override { places.push_back({window, corner}); }
    void send_to_output(std::uint64_t, const std::string&) override {}

    struct Place {
        std::uint64_t window;
        PointF corner;
    };
    std::vector<Place> places;

private:
    OutputLayout layout_;
};

GuestWindow guest_at(int x, int y) {
    WindowFields fields;
    fields.geometry.frame = Rect{x, y, 800, 600};
    fields.geometry.picture = Rect{0, 0, 800, 600};
    return GuestWindow{kWindow, fields};
}

struct Setup {
    rwin::test::FakeKWinBus session_bus;
    PollLoop loop;
    BusConnection bus{loop};
    FakeKWin* kwin = nullptr;
    std::unique_ptr<PlacementSync> sync;
    bool compositor_state = false;
    std::vector<Point> moved_guest;

    Setup() {
        auto owned = std::make_unique<FakeKWin>(loop, bus);
        kwin = owned.get();
        sync = std::make_unique<PlacementSync>(loop, std::move(owned));
        sync->move_guest = [this](WindowId, Point corner) {
            if (compositor_state) return false;
            moved_guest.push_back(corner);
            return true;
        };
        sync->compositor_has_state = [this](WindowId) { return compositor_state; };
        sync->follow_guest(guest_at(100, 100), 1, true, false);
        kwin->window_placed(kWindow.key(), PointF{100, 100}, 0);
        kwin->places.clear();
    }

    void settle() {
        const auto stop = loop.timer([this] { loop.quit(0); });
        stop->start(std::chrono::milliseconds(300), false);
        static_cast<void>(loop.run());
    }

    void follow(int x, int y, bool placeable = true) { sync->follow_guest(guest_at(x, y), 1, placeable, false); }

    void report(BoxF frame, bool dragged, bool by_state) {
        kwin->window_moved(kWindow.key(), KWinPlacement::Report{frame, dragged, by_state, false});
    }
};

constexpr BoxF kDrop{400, 0, 800, 600};
constexpr BoxF kMaximized{0, 0, 1920, 1080};

void drop_then_maximize_does_not_place() {
    Setup setup;
    setup.report(kDrop, true, false);
    check(setup.moved_guest.empty(), "the drop is held, not sent at once");
    setup.settle();
    check(setup.moved_guest.size() == 1 && setup.moved_guest[0] == Point{400, 0}, "the drop is sent to the guest");
    setup.compositor_state = true;
    setup.report(kMaximized, false, false);
    check(setup.moved_guest.size() == 1, "the maximised frame is not sent to the guest");
    setup.follow(400, 0);
    check(setup.kwin->places.empty(), "the guest's report of the drop does not place the window");
    setup.follow(400, 0, false);
    setup.follow(0, 0, false);
    check(setup.kwin->places.empty(), "nor does the guest maximizing");
    setup.report(kMaximized, false, true);
    check(setup.kwin->places.empty() && setup.moved_guest.size() == 1, "the state report changes nothing");
}

void maximize_before_drop_does_not_place() {
    Setup setup;
    setup.compositor_state = true;
    setup.report(kDrop, true, false);
    setup.report(kMaximized, false, false);
    setup.settle();
    setup.follow(400, 0);
    setup.follow(300, 20);
    check(setup.moved_guest.empty() && setup.kwin->places.empty(), "nothing is sent or placed");
}

void refused_move_keeps_the_awaited_corner() {
    Setup setup;
    setup.report(kDrop, true, false);
    setup.settle();
    setup.compositor_state = true;
    setup.report(BoxF{10, 10, 800, 600}, true, false);
    setup.settle();
    setup.follow(400, 0);
    check(setup.kwin->places.empty(), "the corner the guest was sent is still awaited");
}

void user_moves_still_reach_the_guest() {
    Setup setup;
    setup.report(kDrop, true, false);
    setup.settle();
    setup.follow(400, 0);
    setup.report(BoxF{500, 200, 800, 600}, true, false);
    setup.settle();
    check(setup.moved_guest.size() == 2 && setup.moved_guest[1] == Point{500, 200}, "a plain drag is followed");
}

void maximize_within_the_settle_drops_the_move() {
    Setup setup;
    setup.report(kDrop, true, false);
    setup.compositor_state = true;
    setup.report(kMaximized, false, false);
    setup.settle();
    check(setup.moved_guest.empty(), "a maximize configure inside the window sends no guest move");
}

void state_without_a_report_drops_the_move() {
    Setup setup;
    setup.report(kDrop, true, false);
    setup.compositor_state = true;
    setup.settle();
    check(setup.moved_guest.empty(), "a state held when the window ends sends no guest move");
}

void later_drag_replaces_the_held_move() {
    Setup setup;
    setup.report(kDrop, true, false);
    setup.report(BoxF{500, 200, 800, 600}, true, false);
    setup.settle();
    check(setup.moved_guest.size() == 1 && setup.moved_guest[0] == Point{500, 200}, "only the latest drop is sent");
}

}

int main() {
    drop_then_maximize_does_not_place();
    maximize_before_drop_does_not_place();
    refused_move_keeps_the_awaited_corner();
    user_moves_still_reach_the_guest();
    maximize_within_the_settle_drops_the_move();
    state_without_a_report_drops_the_move();
    later_drag_replaces_the_held_move();
    if (failures == 0) std::printf("placement_sync: all passed\n");
    return failures == 0 ? 0 : 1;
}
