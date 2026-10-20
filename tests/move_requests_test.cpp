// MoveRequests: one WindowMove at a time, the newest size and place asked
// together once the last is answered, a resize keeping the edges it does
// not move, and an ask the guest leaves unanswered given up on.

#include "host/chrome/resize_border.hpp"
#include "host/loop/poll_loop.hpp"
#include "host/mirror/move_requests.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

using rwin::host::Box;
using rwin::host::MoveRequests;
using rwin::host::Point;
using rwin::host::PollLoop;
using rwin::host::ResizeBorder;
using rwin::host::Size;

struct Guest {
    PollLoop loop;
    std::vector<Box> asked;
    std::vector<Box> answered;
    MoveRequests moves{loop, [this](const Box& frame) { asked.push_back(frame); },
                       [this](const Box&, const Box& got) { answered.push_back(got); }};

    Guest() { moves.reported(Box{100, 100, 800, 600}); }
};

void coalesces_while_one_is_out() {
    Guest guest;
    guest.moves.want_size(Size{810, 600}, ResizeBorder::kRight);
    guest.moves.want_size(Size{820, 600}, ResizeBorder::kRight);
    guest.moves.want_size(Size{830, 600}, ResizeBorder::kRight);
    check(guest.asked.size() == 1 && guest.asked[0] == Box{100, 100, 810, 600}, "one ask out at a time");
    guest.moves.reported(Box{100, 100, 800, 600});
    check(guest.asked.size() == 1, "a report that changes nothing answers nothing");
    guest.moves.reported(Box{100, 100, 810, 600});
    check(guest.asked.size() == 2 && guest.asked[1] == Box{100, 100, 830, 600}, "the newest is asked next");
    guest.moves.reported(Box{100, 100, 830, 600});
    check(guest.moves.idle() && guest.answered.size() == 2, "both answered, nothing waits");
}

void keeps_the_edges_not_moved() {
    Guest guest;
    guest.moves.want_size(Size{900, 700}, ResizeBorder::kLeft | ResizeBorder::kTop);
    check(guest.asked.size() == 1 && guest.asked[0] == Box{0, 0, 900, 700}, "left and top move, right and bottom stay");
}

void size_and_place_together() {
    Guest guest;
    guest.moves.want_size(Size{810, 600}, ResizeBorder::kRight);
    guest.moves.want_corner(Point{40, 50});
    guest.moves.want_size(Size{820, 610}, ResizeBorder::kRight | ResizeBorder::kBottom);
    guest.moves.reported(Box{100, 100, 810, 600});
    check(guest.asked.size() == 2 && guest.asked[1] == Box{40, 50, 820, 610},
          "a place and a size asked meanwhile go as one move");
}

void nothing_to_ask() {
    Guest guest;
    guest.moves.want_size(Size{800, 600}, ResizeBorder::kRight);
    check(guest.asked.empty() && guest.moves.idle(), "a frame the guest already has is not asked for");
}

void unanswered_is_given_up() {
    Guest guest;
    guest.moves.want_size(Size{810, 600}, ResizeBorder::kRight);
    guest.moves.want_size(Size{820, 600}, ResizeBorder::kRight);
    const auto stop = guest.loop.timer([&] { guest.loop.quit(0); });
    stop->start(std::chrono::milliseconds(400), false);
    static_cast<void>(guest.loop.run());
    check(guest.asked.size() == 2 && guest.asked[1] == Box{100, 100, 820, 600},
          "after the wait the next is asked regardless");
}

}  // namespace

int main() {
    coalesces_while_one_is_out();
    keeps_the_edges_not_moved();
    size_and_place_together();
    nothing_to_ask();
    unanswered_is_given_up();
    if (failures == 0) std::printf("move_requests: all passed\n");
    return failures == 0 ? 0 : 1;
}
