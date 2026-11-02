#pragma once

#include "proto/pointer_button.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace rwin::guest {

class WindowSurface;

[[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> move_pointer_to(const WindowSurface& surface,
                                                                                   std::int32_t x, std::int32_t y);
void press_pointer(const WindowSurface& surface, std::int32_t x, std::int32_t y, PointerButton::Button button,
                   bool pressed);
void press_pointer_in_place(PointerButton::Button button, bool pressed);
void move_pointer_on_screen(std::int32_t screen_x, std::int32_t screen_y);
void press_pointer_on_screen(std::int32_t screen_x, std::int32_t screen_y, PointerButton::Button button,
                             bool pressed);
void turn_wheel(const WindowSurface& surface, std::int32_t x, std::int32_t y, std::int32_t dx, std::int32_t dy);
void turn_wheel_on_screen(std::int32_t screen_x, std::int32_t screen_y, std::int32_t dx, std::int32_t dy);
void turn_wheel_in_place(std::int32_t dx, std::int32_t dy);
void nudge_pointer(std::int32_t dx, std::int32_t dy);

}
