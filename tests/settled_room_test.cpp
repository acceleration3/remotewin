#include "host/mirror/settled_room.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::host::SettledRoom;
using rwin::host::Size;
using rwin::host::ToplevelConfigure;

ToplevelConfigure configure(int width, int height, bool resizing = false) {
    ToplevelConfigure made;
    made.size = Size{width, height};
    made.resizing = resizing;
    return made;
}

void a_window_left_to_its_own_size_tells_nothing() {
    SettledRoom room;
    check(!room.changed(configure(0, 0), 1.0).has_value(), "a configure without a size is not a room");
    check(!room.changed(configure(800, 0), 1.0).has_value(), "nor is one with a side missing");
}

void a_size_is_told_in_device_pixels_once() {
    SettledRoom room;
    check(room.changed(configure(1000, 700), 1.2) == std::optional<Size>(Size{1200, 840}),
          "1000x700 units at 1.2 are 1200x840 pixels");
    check(!room.changed(configure(1000, 700), 1.2).has_value(), "the same room again is not told again");
    ToplevelConfigure active = configure(1000, 700);
    active.activated = true;
    check(!room.changed(active, 1.2).has_value(), "a state change that keeps the size is not told");
    check(room.changed(configure(1001, 701), 1.2) == std::optional<Size>(Size{1201, 841}),
          "a fraction of a pixel rounds to the nearest: 1201.2x841.2 is 1201x841");
    check(room.changed(configure(1001, 701), 1.0) == std::optional<Size>(Size{1001, 701}),
          "the same units on another scale are another room");
}

void a_resize_under_way_is_told_when_it_ends() {
    SettledRoom room;
    check(room.changed(configure(800, 600), 1.0).has_value(), "the room before the drag");
    bool quiet = true;
    for (int width = 801; width <= 900; ++width) {
        if (room.changed(configure(width, 600, true), 1.0).has_value()) quiet = false;
    }
    check(quiet, "no size of a resize under way is told");
    check(room.changed(configure(900, 600), 1.0) == std::optional<Size>(Size{900, 600}),
          "the size the resize ended on is told");
    for (int width = 899; width >= 800; --width) static_cast<void>(room.changed(configure(width, 600, true), 1.0));
    check(!room.changed(configure(900, 600), 1.0).has_value(), "a drag that ends where it began tells nothing");
}

}

int main() {
    a_window_left_to_its_own_size_tells_nothing();
    a_size_is_told_in_device_pixels_once();
    a_resize_under_way_is_told_when_it_ends();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
