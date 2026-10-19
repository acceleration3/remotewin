#include "guest/windows/window_commands.hpp"

#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>

namespace rwin::guest {

namespace {

// Undocumented; what RAIL's server posts for Client System Menu.
constexpr UINT kPopupSystemMenu = 0x0313;
// The Alt+Space WM_SYSCHAR that precedes SC_KEYMENU: repeat 1, scan code
// 0x39, context (Alt held) set.
constexpr LPARAM kAltSpace = 0x20390001;

std::int32_t kept_in(std::int32_t at, std::int32_t room_from, std::uint32_t room_size, std::uint32_t size) {
    return std::clamp(at, room_from, room_from + static_cast<std::int32_t>(room_size - std::min(size, room_size)));
}

}  // namespace

void post_sys_command(void* window, SysCommand::Command command) {
    const auto hwnd = static_cast<HWND>(window);
    const auto code = static_cast<WPARAM>(command);
    switch (command) {
        case SysCommand::Command::KeyMenu:
            ::PostMessageW(hwnd, WM_SYSCHAR, L' ', kAltSpace);
            ::PostMessageW(hwnd, WM_SYSCOMMAND, code, L' ');
            return;
        case SysCommand::Command::Restore:
            if (::IsIconic(hwnd) != FALSE && ::IsWindowEnabled(hwnd) == FALSE) {
                ::ShowWindowAsync(hwnd, SW_RESTORE);
                return;
            }
            break;
        default:
            break;
    }
    ::PostMessageW(hwnd, WM_SYSCOMMAND, code, 0);
}

void post_system_menu(void* window, std::int32_t screen_x, std::int32_t screen_y) {
    ::PostMessageW(static_cast<HWND>(window), kPopupSystemMenu, 0, MAKELPARAM(screen_x, screen_y));
}

void place(void* window, const Rect& frame, const Rect& room) {
    const auto hwnd = static_cast<HWND>(window);
    if (::IsIconic(hwnd) != FALSE || ::IsZoomed(hwnd) != FALSE) return;
    const WindowSurface surface{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window))};
    if (!surface.valid()) return;
    const bool resized = frame.width != surface.width() || frame.height != surface.height();
    const std::uint32_t width = resized ? std::min(frame.width, room.width) : frame.width;
    const std::uint32_t height = resized ? std::min(frame.height, room.height) : frame.height;
    const std::int32_t x = resized ? kept_in(frame.x, room.x, room.width, width) : frame.x;
    const std::int32_t y = resized ? kept_in(frame.y, room.y, room.height, height) : frame.y;
    // SWP_ASYNCWINDOWPOS: the window's own thread applies it, and a hung
    // one does not hang this.
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
    if (!resized) flags |= SWP_NOSIZE;
    if (::SetWindowPos(hwnd, nullptr, x - surface.inset_x(), y - surface.inset_y(),
                       static_cast<int>(width + surface.border_width()),
                       static_cast<int>(height + surface.border_height()), flags)
        == FALSE) {
        RWIN_WARN("move", "{:#x} could not be put at {},{} {}x{}: error {}", reinterpret_cast<std::uintptr_t>(window),
                  x, y, width, height, ::GetLastError());
        return;
    }
    RWIN_DEBUG("move", "{:#x} frame to {},{} {}x{}", reinterpret_cast<std::uintptr_t>(window), x, y, width, height);
}

}  // namespace rwin::guest
