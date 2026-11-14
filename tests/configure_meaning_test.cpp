#include "host/mirror/configure_meaning.hpp"

#include <cstdio>
#include <optional>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::host::ConfigureMeaning;
using rwin::host::Size;
using rwin::host::ToplevelConfigure;
using Kind = rwin::host::ConfigureMeaning::Kind;

constexpr Size kOutput{1920, 1080};
constexpr Size kWorkArea{1920, 1040};
constexpr Size kHyprlandAlone{1880, 1000};
constexpr Size kHyprlandHalf{935, 1000};

ConfigureMeaning::Guest guest(Size size) { return ConfigureMeaning::Guest{size, Size{}, Size{}, true}; }

ToplevelConfigure hyprland(Size size) {
    ToplevelConfigure configure;
    configure.size = size;
    configure.maximized = true;
    configure.tiled = true;
    configure.activated = true;
    return configure;
}

ToplevelConfigure kwin(Size size) {
    ToplevelConfigure configure;
    configure.size = size;
    configure.bounds = kWorkArea;
    configure.activated = true;
    return configure;
}

void hyprland_tile_beside_another_window() {
    const ConfigureMeaning meaning = ConfigureMeaning::of(hyprland(kHyprlandHalf), guest(Size{1280, 800}));
    check(meaning.kind == Kind::Placed && !meaning.held(), "a tile said maximized is a placement, not a maximise");
    check(meaning.guest_size == std::optional<Size>(kHyprlandHalf), "the guest window takes the tile's size");
}

void hyprland_split_when_a_second_window_opens() {
    const ConfigureMeaning alone = ConfigureMeaning::of(hyprland(kHyprlandAlone), guest(Size{1280, 800}));
    check(alone.kind == Kind::Placed && alone.guest_size == std::optional<Size>(kHyprlandAlone),
          "alone on the workspace the guest window takes the whole tile");
    const ConfigureMeaning split = ConfigureMeaning::of(hyprland(kHyprlandHalf), guest(kHyprlandAlone));
    check(split.kind == Kind::Placed && split.guest_size == std::optional<Size>(kHyprlandHalf),
          "and half of it when another window opens");
    const ConfigureMeaning again = ConfigureMeaning::of(hyprland(kHyprlandHalf), guest(kHyprlandHalf));
    check(again.kind == Kind::Placed && !again.guest_size.has_value(), "a tile the guest already fills asks nothing");
}

void hyprland_floating_window_said_maximized() {
    const ConfigureMeaning kept = ConfigureMeaning::of(hyprland(Size{800, 600}), guest(Size{800, 600}));
    check(kept.kind == Kind::Placed && !kept.guest_size.has_value(), "a floating window at its own size stays as it is");
    const ConfigureMeaning resized = ConfigureMeaning::of(hyprland(Size{1000, 700}), guest(Size{800, 600}));
    check(resized.kind == Kind::Placed && resized.guest_size == std::optional<Size>(Size{1000, 700}),
          "one the compositor resized is followed");
}

void hyprland_maximize_and_fullscreen() {
    const ConfigureMeaning maximized = ConfigureMeaning::of(hyprland(kHyprlandAlone), guest(kHyprlandHalf));
    check(maximized.kind == Kind::Placed && maximized.guest_size == std::optional<Size>(kHyprlandAlone),
          "Hyprland's own maximise is one more placement");
    const ConfigureMeaning uncovered = ConfigureMeaning::of(hyprland(kOutput), guest(kHyprlandHalf));
    check(uncovered.kind == Kind::Placed, "also with no gaps and no bar: nothing says it is more than a tile");
    ToplevelConfigure full = hyprland(kOutput);
    full.fullscreen = true;
    const ConfigureMeaning fullscreen = ConfigureMeaning::of(full, guest(kHyprlandHalf));
    check(fullscreen.kind == Kind::Fullscreen && fullscreen.held() && !fullscreen.guest_size.has_value(),
          "fullscreen is held and asks no size");
}

void hyprland_before_the_window_is_mapped() {
    ToplevelConfigure first;
    first.size = kHyprlandHalf;
    first.tiled = true;
    const ConfigureMeaning meaning = ConfigureMeaning::of(first, guest(Size{1280, 800}));
    check(meaning.kind == Kind::Free && meaning.guest_size == std::optional<Size>(kHyprlandHalf),
          "the tile's size is taken before the maximized state arrives");
}

void a_placement_being_resized_waits() {
    ToplevelConfigure dragging = hyprland(Size{700, 1000});
    dragging.resizing = true;
    const ConfigureMeaning during = ConfigureMeaning::of(dragging, guest(kHyprlandHalf));
    check(during.kind == Kind::Placed && !during.guest_size.has_value(), "nothing is asked while the tile is resized");
    const ConfigureMeaning after = ConfigureMeaning::of(hyprland(Size{700, 1000}), guest(kHyprlandHalf));
    check(after.guest_size == std::optional<Size>(Size{700, 1000}), "the settled size is");
}

void a_guest_that_cannot_take_the_size() {
    ConfigureMeaning::Guest dialog = guest(Size{420, 260});
    dialog.sizable = false;
    const ConfigureMeaning fixed = ConfigureMeaning::of(hyprland(kHyprlandHalf), dialog);
    check(fixed.kind == Kind::Placed && !fixed.guest_size.has_value(), "a window of fixed size is asked nothing");
    ConfigureMeaning::Guest limited = guest(Size{1280, 800});
    limited.smallest = Size{1000, 500};
    limited.largest = Size{1600, 900};
    const ConfigureMeaning narrow = ConfigureMeaning::of(hyprland(kHyprlandHalf), limited);
    check(narrow.guest_size == std::optional<Size>(Size{1000, 900}), "the size asked stays within the guest's limits");
    ConfigureMeaning::Guest at_limit = limited;
    at_limit.size = Size{1000, 900};
    check(!ConfigureMeaning::of(hyprland(kHyprlandHalf), at_limit).guest_size.has_value(),
          "and is not asked again once the guest is at its limit");
    ConfigureMeaning::Guest unbounded = guest(Size{1280, 800});
    unbounded.smallest = Size{0, 300};
    check(ConfigureMeaning::of(hyprland(Size{500, 200}), unbounded).guest_size == std::optional<Size>(Size{500, 300}),
          "a side with no limit is taken as configured");
}

void kwin_maximize_and_restore() {
    ToplevelConfigure maximized = kwin(kWorkArea);
    maximized.maximized = true;
    const ConfigureMeaning held = ConfigureMeaning::of(maximized, guest(Size{1280, 800}));
    check(held.kind == Kind::Maximized && held.held() && !held.guest_size.has_value(),
          "KWin's maximized is a maximise and asks no size");
    ToplevelConfigure other_output = kwin(Size{1600, 860});
    other_output.maximized = true;
    check(ConfigureMeaning::of(other_output, guest(Size{1280, 800})).kind == Kind::Maximized,
          "also on an output smaller than the bounds KWin once gave");
    ToplevelConfigure unbounded = maximized;
    unbounded.bounds = Size{};
    check(ConfigureMeaning::of(unbounded, guest(Size{1280, 800})).kind == Kind::Maximized,
          "and from a compositor that gives no bounds");
    const ConfigureMeaning restored = ConfigureMeaning::of(kwin(Size{1280, 800}), guest(kWorkArea));
    check(restored.kind == Kind::Free && restored.guest_size == std::optional<Size>(Size{1280, 800}),
          "the restored size is asked of the guest");
}

void kwin_quick_tile_and_resize() {
    ToplevelConfigure half = kwin(Size{960, 1040});
    half.tiled = true;
    const ConfigureMeaning tiled = ConfigureMeaning::of(half, guest(Size{1280, 800}));
    check(tiled.kind == Kind::Free && tiled.guest_size == std::optional<Size>(Size{960, 1040}),
          "a screen half is a size to take, with no state held");
    ToplevelConfigure dragging = kwin(Size{900, 700});
    dragging.resizing = true;
    const ConfigureMeaning during = ConfigureMeaning::of(dragging, guest(Size{1280, 800}));
    check(during.kind == Kind::Free && during.guest_size == std::optional<Size>(Size{900, 700}),
          "a resize by the user is followed while it lasts");
    const ConfigureMeaning unsized = ConfigureMeaning::of(kwin(Size{}), guest(Size{1280, 800}));
    check(unsized.kind == Kind::Free && !unsized.guest_size.has_value(), "a configure with no size leaves the guest's");
    ToplevelConfigure full = kwin(kOutput);
    full.fullscreen = true;
    check(ConfigureMeaning::of(full, guest(Size{1280, 800})).kind == Kind::Fullscreen, "fullscreen is fullscreen");
}

void a_compositor_that_gives_its_bounds() {
    ToplevelConfigure filling = hyprland(kWorkArea);
    filling.bounds = kWorkArea;
    check(ConfigureMeaning::of(filling, guest(Size{1280, 800})).kind == Kind::Maximized,
          "maximized and tiled over the whole bounds is a maximise");
    ToplevelConfigure tile = hyprland(Size{960, 1040});
    tile.bounds = kWorkArea;
    check(ConfigureMeaning::of(tile, guest(Size{1280, 800})).kind == Kind::Placed,
          "over less than the bounds it is a placement");
}

}

int main() {
    hyprland_tile_beside_another_window();
    hyprland_split_when_a_second_window_opens();
    hyprland_floating_window_said_maximized();
    hyprland_maximize_and_fullscreen();
    hyprland_before_the_window_is_mapped();
    a_placement_being_resized_waits();
    a_guest_that_cannot_take_the_size();
    kwin_maximize_and_restore();
    kwin_quick_tile_and_resize();
    a_compositor_that_gives_its_bounds();
    if (failures == 0) std::printf("configure_meaning: all passed\n");
    return failures == 0 ? 0 : 1;
}
