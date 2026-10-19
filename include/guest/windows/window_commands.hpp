#pragma once

#include "proto/rect.hpp"
#include "proto/sys_command.hpp"

#include <cstdint>

namespace rwin::guest {

// What the host asks done to a window, done as Windows' own RAIL server
// does it (docs/rail-model.md §2.5, §2.7): posted, never sent, so a hung
// window holds nothing up.

// WM_SYSCOMMAND, so the window's own handling decides - SC_CLOSE, not
// WM_CLOSE. SC_KEYMENU is preceded by the Alt+Space it stands for, and
// SC_RESTORE of a minimised window disabled behind its modal dialog, which
// ignores WM_SYSCOMMAND, is ShowWindowAsync.
void post_sys_command(void* window, SysCommand::Command command);

// WM_POPUPSYSTEMMENU at a point of the screen.
void post_system_menu(void* window, std::int32_t screen_x, std::int32_t screen_y);

// A popup the host's compositor dismissed, closed: a menu as a press
// outside it closes one - WM_CANCELMODE to the window whose menu it is,
// which is EndMenu for a thread this one cannot call it on - a combo box's
// list by the box, anything else by an Escape.
void dismiss_popup(void* window);

// The window's frame put at ``frame``, without activating it or changing
// its place in the z-order. A new size is held to ``room``, the work area,
// the window moved in by as much as it would hang past it. A minimised or
// maximised window is where its state puts it, and stays.
void place(void* window, const Rect& frame, const Rect& room);

}  // namespace rwin::guest
