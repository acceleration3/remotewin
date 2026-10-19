#pragma once

#include "proto/window_fields.hpp"

#include <cstdint>
#include <optional>

namespace rwin::guest {

// Where a popup was meant to open, read from what Windows keeps about the
// menu, list or tip it is rather than from where Windows put it
// (docs/review-and-redesign.md R2): Windows flips and slides a popup
// against the guest's own screen, which is not the screen the host shows
// it on, so the host is told the intent and its compositor places it.
//
//   menu bar menu  the highlighted menu-bar item (GetMenuBarInfo), opening
//                  below it
//   submenu        its item in the menu it opens out of (MN_GETHMENU,
//                  GetMenuItemRect), opening to the right
//   combo list     the combo box it drops from (GetComboBoxInfo), opening
//                  below it
//   context menu   the pointer where it opened, when Windows put a corner
//                  of the menu there
//   tooltip        the pointer where it opened, at Windows' own offset
//   anything else  where Windows put it, against its owner or the window
//                  it was opened from
//
// A right-to-left window's menus open the other way, as Windows opens them.
class PopupIntent {
public:
    struct Point {
        std::int32_t x = 0;
        std::int32_t y = 0;

        [[nodiscard]] bool operator==(const Point&) const noexcept = default;
    };

    // ``frame`` is the window's own, on the screen; ``opened_at`` where the
    // pointer was when it was first seen shown.
    PopupIntent(void* window, const Rect& frame, std::optional<Point> opened_at);

    // The window it opens against; null when it is no popup of anything.
    [[nodiscard]] void* anchor() const noexcept { return anchor_; }
    // With ``anchor`` none: the tracker names the anchor.
    [[nodiscard]] const WindowFields::Popup& popup() const noexcept { return popup_; }

private:
    using Edge = WindowFields::Popup::Edge;
    using Kind = WindowFields::Popup::Kind;

    void read_menu(void* window, const std::optional<Point>& opened_at);
    [[nodiscard]] bool read_menu_bar_item(void* menu, void* owner);
    [[nodiscard]] bool read_parent_item(void* window, void* menu);
    void read_combo_list(void* window);
    void read_tooltip(void* window, const std::optional<Point>& opened_at);
    void read_placed_against(void* window);
    // The anchor and what is relative to its frame, from screen rectangles.
    void open(Kind kind, void* anchor, std::int32_t left, std::int32_t top, std::int32_t right,
              std::int32_t bottom);
    void point_at(Kind kind, void* anchor, Point point);
    void direct(Edge edge, Edge gravity, std::uint8_t adjust, bool rtl);

    Rect frame_;
    void* anchor_ = nullptr;
    WindowFields::Popup popup_;
};

}  // namespace rwin::guest
