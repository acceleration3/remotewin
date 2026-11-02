#pragma once

#include "proto/window_fields.hpp"

#include <cstdint>
#include <optional>

namespace rwin::guest {
class PopupIntent {
public:
    struct Point {
        std::int32_t x = 0;
        std::int32_t y = 0;

        [[nodiscard]] bool operator==(const Point&) const noexcept = default;
    };

    PopupIntent(void* window, const Rect& frame, std::optional<Point> opened_at);

    [[nodiscard]] void* anchor() const noexcept { return anchor_; }
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
    void open(Kind kind, void* anchor, std::int32_t left, std::int32_t top, std::int32_t right,
              std::int32_t bottom);
    void point_at(Kind kind, void* anchor, Point point);
    void direct(Edge edge, Edge gravity, std::uint8_t adjust, bool rtl);

    Rect frame_;
    void* anchor_ = nullptr;
    WindowFields::Popup popup_;
};

}
