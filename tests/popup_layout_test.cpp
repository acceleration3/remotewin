#include "host/geometry/view_mapping.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "host/mirror/mirror_role.hpp"
#include "host/mirror/popup_layout.hpp"
#include "proto/window_fields.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::Box;
using rwin::host::GuestWindow;
using rwin::host::GuestWindows;
using rwin::host::MirrorRole;
using rwin::host::Point;
using rwin::host::PopupPlacement;
using rwin::host::Size;
using rwin::host::ViewMapping;
using Popup = WindowFields::Popup;
using Kind = MirrorRole::Kind;

constexpr WindowId kMain{0x10, 1};
constexpr WindowId kMenu{0x20, 2};
constexpr std::uint32_t kWsCaption = 0x00C00000;
constexpr std::uint32_t kWsPopup = 0x80000000;
constexpr std::uint32_t kWsExNoActivate = 0x08000000;

WindowFields shown(rwin::Rect frame) {
    WindowFields fields;
    fields.show = WindowFields::Show::Shown;
    fields.geometry.frame = frame;
    fields.geometry.picture = rwin::Rect{0, 0, frame.width, frame.height};
    return fields;
}

GuestWindow main_window() { return GuestWindow{kMain, shown({100, 100, 800, 600})}; }

GuestWindow menu(Popup intent, rwin::Rect frame) {
    WindowFields fields = shown(frame);
    fields.style.style = kWsPopup;
    fields.popup = intent;
    return GuestWindow{kMenu, fields};
}

void a_menu_bar_menu_opens_below_its_item() {
    const Popup intent{kMain, Popup::Kind::MenuBar, {10, 50, 200, 300}, {10, 30, 40, 20}, Popup::Edge::BottomLeft,
                       Popup::Edge::BottomRight, Popup::kFlipY | Popup::kSlideX | Popup::kSlideY};
    const GuestWindow parent = main_window();
    const PopupPlacement placement = rwin::host::popup_layout(menu(intent, {110, 150, 200, 300}), parent,
                                                              ViewMapping::one_to_one(Size{800, 600}, 1.0),
                                                              Size{200, 300});
    check(placement.anchor == (Box{10, 30, 40, 20}), "from the item, on the parent's surface");
    check(placement.edge == 6 && placement.gravity == 8, "at its bottom left, opening down and right");
    check(placement.adjust == (Popup::kFlipY | Popup::kSlideX | Popup::kSlideY), "flipping up where there is no room");
    check(placement.offset == (Point{0, 0}) && placement.size == (Size{200, 300}), "no offset; its own size");
}

void intent_is_in_the_parents_units_and_picture() {
    const Popup intent{kMain, Popup::Kind::Submenu, {0, 0, 100, 100}, {20, 40, 100, 20}, Popup::Edge::TopRight,
                       Popup::Edge::BottomRight, Popup::kFlipX};
    WindowFields fields = shown({100, 100, 800, 600});
    fields.geometry.picture = rwin::Rect{10, 0, 780, 600};
    const GuestWindow trimmed{kMain, fields};
    const PopupPlacement placement = rwin::host::popup_layout(menu(intent, {0, 0, 100, 100}), trimmed,
                                                              ViewMapping::one_to_one(Size{780, 600}, 1.25),
                                                              Size{80, 80});
    check(placement.anchor == (Box{8, 32, 80, 16}), "frame-relative, less the trimmed margin, at 1.25");
}

void a_popup_without_intent_opens_where_windows_put_it() {
    const GuestWindow parent = main_window();
    const Popup none{kMain, Popup::Kind::Exact, {50, 640, 100, 50}, {50, 640, 100, 50}, Popup::Edge::None,
                     Popup::Edge::None, 0};
    const PopupPlacement placement = rwin::host::popup_layout(menu(none, {150, 740, 100, 50}), parent,
                                                              ViewMapping::one_to_one(Size{800, 600}, 1.0),
                                                              Size{100, 50});
    check(placement.anchor == (Box{50, 599, 1, 1}), "a unit at the point, kept inside the parent as an anchor has to be");
    check(placement.offset == (Point{0, 41}), "the rest of the way as an offset");
    check(placement.edge == 5 && placement.gravity == 8, "its top left at that point");
}

void a_tooltip_keeps_windows_offset_from_the_pointer() {
    const GuestWindow parent = main_window();
    const Popup tip{kMain, Popup::Kind::Tooltip, {210, 230, 120, 24}, {200, 200, 0, 0}, Popup::Edge::TopLeft,
                    Popup::Edge::BottomRight, Popup::kFlipY};
    const PopupPlacement placement = rwin::host::popup_layout(menu(tip, {310, 330, 120, 24}), parent,
                                                              ViewMapping::one_to_one(Size{800, 600}, 1.0),
                                                              Size{120, 24});
    check(placement.anchor == (Box{200, 200, 1, 1}), "from the pointer");
    check(placement.offset == (Point{10, 30}), "where Windows put it from there");
}

GuestWindows windows_of(std::initializer_list<GuestWindow> windows) {
    GuestWindows known;
    for (const GuestWindow& window : windows) static_cast<void>(known.tell(window.id(), window.fields()));
    return known;
}

void roles() {
    const GuestWindow parent = main_window();
    check(MirrorRole::of(parent, windows_of({parent})) == MirrorRole{}, "a window nothing owns is a toplevel");

    WindowFields dialog = shown({200, 200, 300, 200});
    dialog.owner = kMain;
    dialog.style.style = kWsCaption;
    const GuestWindow dialog_window{WindowId{0x30, 3}, dialog};
    check(MirrorRole::of(dialog_window, windows_of({parent, dialog_window})) == (MirrorRole{Kind::Toplevel, kMain}),
          "an owned dialog is a toplevel kept above its owner");

    const Popup intent{kMain, Popup::Kind::MenuBar, {}, {}, Popup::Edge::BottomLeft, Popup::Edge::BottomRight, 0};
    const GuestWindow opened = menu(intent, {110, 150, 200, 300});
    check(MirrorRole::of(opened, windows_of({parent, opened})) == (MirrorRole{Kind::Popup, kMain}),
          "a menu is a popup of the window it opens from");

    WindowFields part = shown({120, 120, 50, 50});
    part.owner = kMain;
    part.style.style = kWsPopup;
    part.style.extended = kWsExNoActivate;
    const GuestWindow inside{WindowId{0x40, 4}, part};
    check(MirrorRole::of(inside, windows_of({parent, inside})) == (MirrorRole{Kind::Subsurface, kMain}),
          "an owned window that never activates and lies inside its owner is part of it");
    part.geometry.frame = rwin::Rect{850, 120, 100, 50};
    const GuestWindow outside{WindowId{0x40, 4}, part};
    check(MirrorRole::of(outside, windows_of({parent, outside})) == (MirrorRole{Kind::Popup, kMain}),
          "one that hangs outside it is a popup");

    WindowFields shadow = shown({90, 90, 820, 620});
    shadow.owner = kMain;
    shadow.z_glue = true;
    const GuestWindow glued{WindowId{0x50, 5}, shadow};
    check(MirrorRole::of(glued, windows_of({parent, glued})) == (MirrorRole{Kind::Subsurface, kMain}),
          "a window kept with its neighbours is part of them");

    check(MirrorRole::of(opened, windows_of({opened})) == MirrorRole{}, "a popup of nothing known stands alone");
}

}

int main() {
    a_menu_bar_menu_opens_below_its_item();
    intent_is_in_the_parents_units_and_picture();
    a_popup_without_intent_opens_where_windows_put_it();
    a_tooltip_keeps_windows_offset_from_the_pointer();
    roles();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("popup layout: all checks passed\n");
    return EXIT_SUCCESS;
}
