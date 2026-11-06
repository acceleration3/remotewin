#include "guest/windows/maximized_position.hpp"

#include <chrono>
#include <cstdio>

namespace {

using rwin::Rect;
using rwin::guest::MaximizedPosition;
using Spot = MaximizedPosition::Spot;

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

int window_a = 0;
int window_b = 0;
const MaximizedPosition::Clock::time_point start{};

void measured_case_is_moved_back() {
    MaximizedPosition position;
    const auto fix = position.correction(&window_a, true, Rect{733, 0, 2578, 1398}, Rect{0, 0, 2560, 1380}, start);
    check(fix.has_value() && *fix == Spot{-9, -9}, "733,0 goes back to -9,-9");
}

void correct_window_is_left() {
    MaximizedPosition position;
    check(!position.correction(&window_a, true, Rect{-9, -9, 2578, 1398}, Rect{0, 0, 2560, 1380}, start),
          "a window at -9,-9 is left");
    check(!position.correction(&window_a, true, Rect{-8, -10, 2578, 1398}, Rect{0, 0, 2560, 1380}, start),
          "a pixel off is left");
}

void window_not_zoomed_is_left() {
    MaximizedPosition position;
    check(!position.correction(&window_a, false, Rect{733, 0, 2578, 1398}, Rect{0, 0, 2560, 1380}, start),
          "a window that is not maximized is left");
}

void second_monitor_uses_its_work_area() {
    MaximizedPosition position;
    const auto fix = position.correction(&window_a, true, Rect{3000, 400, 1938, 1098}, Rect{2560, 40, 1920, 1080}, start);
    check(fix.has_value() && *fix == Spot{2551, 31}, "an offset work area gives its own centred corner");
}

void insisting_app_is_not_looped() {
    MaximizedPosition position;
    const Rect off{733, 0, 2578, 1398};
    const Rect work{0, 0, 2560, 1380};
    check(position.correction(&window_a, true, off, work, start).has_value(), "the first is corrected");
    check(!position.correction(&window_a, true, off, work, start + std::chrono::milliseconds(50)),
          "not again at once");
    check(position.correction(&window_b, true, off, work, start + std::chrono::milliseconds(50)).has_value(),
          "another window is independent");
    check(position.correction(&window_a, true, off, work, start + std::chrono::milliseconds(400)).has_value(),
          "again after a while");
    int more = 0;
    for (int step = 1; step <= 20; ++step) {
        if (position.correction(&window_a, true, off, work, start + std::chrono::seconds(step)).has_value()) ++more;
    }
    check(more < 4, "a few times per maximize at most");
    check(!position.correction(&window_a, false, off, work, start + std::chrono::seconds(30)), "restored");
    check(position.correction(&window_a, true, off, work, start + std::chrono::seconds(31)).has_value(),
          "a new maximize is corrected again");
}

}

int main() {
    measured_case_is_moved_back();
    correct_window_is_left();
    window_not_zoomed_is_left();
    second_monitor_uses_its_work_area();
    insisting_app_is_not_looped();
    if (failures == 0) std::printf("maximized_position: all passed\n");
    return failures == 0 ? 0 : 1;
}
