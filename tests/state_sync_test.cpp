#include "host/mirror/state_sync.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

constexpr std::int64_t kMs = 1'000'000;

struct Sides {
    std::vector<bool> guest_asked;
    std::vector<bool> compositor_asked;
    rwin::host::StateSync sync{[this](bool state) { guest_asked.push_back(state); },
                               [this](bool state) { compositor_asked.push_back(state); }};

    Sides() {
        sync.guest_says(false, 0);
        sync.compositor_says(false, 0);
    }
};

void compositor_maximises_past_a_stale_report() {
    Sides sides;
    sides.sync.compositor_says(true, 10 * kMs);
    check(sides.guest_asked == std::vector<bool>{true}, "the guest is asked to maximise once");
    sides.sync.guest_says(false, 20 * kMs);
    check(sides.compositor_asked.empty(), "a stale report asks nothing of the compositor");
    sides.sync.guest_says(true, 40 * kMs);
    check(sides.compositor_asked.empty() && sides.guest_asked.size() == 1, "the guest's answer is not sent back");
    sides.sync.compositor_says(false, 900 * kMs);
    check(sides.guest_asked == std::vector<bool>{true, false}, "a restore after it is the user's, and asked");
}

void guest_toggles_quickly() {
    Sides sides;
    sides.sync.guest_says(true, 10 * kMs);
    sides.sync.guest_says(false, 20 * kMs);
    check(sides.compositor_asked == std::vector<bool>{true, false}, "the compositor is asked for each");
    sides.sync.compositor_says(true, 30 * kMs);
    sides.sync.compositor_says(false, 40 * kMs);
    check(sides.guest_asked.empty(), "the compositor's answers are not asked of the guest");
    check(!sides.sync.compositor_expected(), "the compositor is left restored");
}

void guest_restores_itself() {
    Sides sides;
    sides.sync.guest_says(true, 0);
    sides.sync.compositor_says(true, 5 * kMs);
    sides.guest_asked.clear();
    sides.compositor_asked.clear();
    sides.sync.guest_says(false, 2000 * kMs);
    check(sides.compositor_asked == std::vector<bool>{false}, "the compositor is asked to restore");
    sides.sync.compositor_says(false, 2010 * kMs);
    check(sides.guest_asked.empty(), "and its answer is not sent back");
}

void unanswered_ask_expires() {
    Sides sides;
    sides.sync.compositor_says(true, 0);
    check(sides.guest_asked == std::vector<bool>{true}, "the guest is asked");
    sides.sync.guest_says(false, 2000 * kMs);
    check(!sides.sync.guest_heading(), "after the transit time the ask is dropped");
    sides.sync.guest_says(true, 2100 * kMs);
    check(sides.compositor_asked.empty(), "the guest catching up with the compositor asks nothing");
}

void wanted_once() {
    Sides sides;
    sides.sync.guest_says(true, 0);
    sides.sync.compositor_says(true, 1 * kMs);
    sides.guest_asked.clear();
    sides.sync.compositor_says(false, 2000 * kMs);
    sides.sync.want_guest(false, 2001 * kMs);
    check(sides.guest_asked == std::vector<bool>{false}, "the guest is asked to restore once");
}

void new_surface_asked_again() {
    Sides sides;
    sides.sync.guest_says(true, 0);
    sides.sync.compositor_says(true, 1 * kMs);
    sides.compositor_asked.clear();
    sides.sync.compositor_anew(2000 * kMs);
    check(sides.compositor_asked == std::vector<bool>{true}, "the new surface is asked to maximise");
}

}

int main() {
    compositor_maximises_past_a_stale_report();
    guest_toggles_quickly();
    guest_restores_itself();
    unanswered_ask_expires();
    wanted_once();
    new_surface_asked_again();
    if (failures == 0) std::printf("state_sync: all passed\n");
    return failures == 0 ? 0 : 1;
}
