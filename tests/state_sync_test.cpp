// StateSync: a state changed on one side is asked of the other once, its
// answer is not sent back, and what a side says while an ask of it is on
// its way is not taken for a change - the maximise and minimise ping-pong
// between a mirror and its guest window.

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

// The compositor maximises; the guest reports again (a title, a frame
// settling) before its maximise lands: the stale report undoes nothing.
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

// The guest maximises and restores itself quickly (a double click on its
// caption) before the compositor answered the first: the compositor ends
// restored, and nothing is asked back of the guest.
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

// The guest restores on its own while the compositor is maximised: the
// compositor is asked to restore, and its configure is not sent back.
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

// An ask the other side never answers is given up on: what the side says
// afterwards is believed again.
void unanswered_ask_expires() {
    Sides sides;
    sides.sync.compositor_says(true, 0);
    check(sides.guest_asked == std::vector<bool>{true}, "the guest is asked");
    sides.sync.guest_says(false, 2000 * kMs);
    check(!sides.sync.guest_heading(), "after the transit time the ask is dropped");
    sides.sync.guest_says(true, 2100 * kMs);
    check(sides.compositor_asked.empty(), "the guest catching up with the compositor asks nothing");
}

// Something on this side wants the guest restored (focus came to a mirror
// of a minimised window) while the compositor's unminimise is already on
// its way to it: one restore, not two.
void wanted_once() {
    Sides sides;
    sides.sync.guest_says(true, 0);
    sides.sync.compositor_says(true, 1 * kMs);
    sides.guest_asked.clear();
    sides.sync.compositor_says(false, 2000 * kMs);
    sides.sync.want_guest(false, 2001 * kMs);
    check(sides.guest_asked == std::vector<bool>{false}, "the guest is asked to restore once");
}

// A new surface is asked again for what the guest has.
void new_surface_asked_again() {
    Sides sides;
    sides.sync.guest_says(true, 0);
    sides.sync.compositor_says(true, 1 * kMs);
    sides.compositor_asked.clear();
    sides.sync.compositor_anew(2000 * kMs);
    check(sides.compositor_asked == std::vector<bool>{true}, "the new surface is asked to maximise");
}

}  // namespace

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
