#pragma once

#include "proto/rect.hpp"
#include "proto/sys_command.hpp"

#include <cstdint>

namespace rwin::guest {
void post_sys_command(void* window, SysCommand::Command command);

void post_system_menu(void* window, std::int32_t screen_x, std::int32_t screen_y);

void dismiss_popup(void* window);

void place(void* window, const Rect& frame, const Rect& room);

}
