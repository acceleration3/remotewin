#include "guest/input/pointer_grab_watcher.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

PointerGrabRule::Point cursor_now() {
    POINT at{};
    ::GetCursorPos(&at);
    return {at.x, at.y};
}

// Whether a window is being moved or sized by Windows' own loop, which
// clips the cursor to the work area while it runs: Windows' clip, not one
// the application asked for, and confining the mirror to it would hold the
// pointer inside the very window being dragged.
bool moving_or_sizing() {
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    return ::GetGUIThreadInfo(0, &gui) != FALSE && (gui.flags & GUI_INMOVESIZE) != 0;
}

// GetClipCursor answers the whole desktop when nothing is clipped.
std::optional<PointerGrab::Area> clip_now() {
    if (moving_or_sizing()) return std::nullopt;
    RECT clip{};
    if (::GetClipCursor(&clip) == FALSE) return std::nullopt;
    const int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const RECT desktop{left, top, left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
                       top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    if (clip.left <= desktop.left && clip.top <= desktop.top && clip.right >= desktop.right
        && clip.bottom >= desktop.bottom) {
        return std::nullopt;
    }
    return PointerGrab::Area{clip.left, clip.top, clip.right, clip.bottom};
}

bool cursor_shown() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) == FALSE) return true;
    return (info.flags & CURSOR_SHOWING) != 0 && (info.flags & CURSOR_SUPPRESSED) == 0 && info.hCursor != nullptr;
}

const char* kind_name(PointerGrab::Kind kind) {
    switch (kind) {
        case PointerGrab::Kind::Confine:
            return "confine";
        case PointerGrab::Kind::Lock:
            return "lock";
        case PointerGrab::Kind::None:
            break;
    }
    return "none";
}

}  // namespace

std::optional<PointerGrab> PointerGrabWatcher::changed(WindowId foreground) {
    const PointerGrabRule::Look look{foreground, cursor_shown(), clip_now(), cursor_now(), ::GetTickCount64()};
    const PointerGrab grab = rule_.grab_for(look);
    if (grab == last_) return std::nullopt;
    last_ = grab;
    RWIN_INFO("pointer", "grab {} for {}: area {},{}-{},{} held at {},{} (cursor {}, warping {})",
              kind_name(grab.kind), grab.window, grab.area.left, grab.area.top, grab.area.right, grab.area.bottom,
              grab.x, grab.y, look.cursor_shown ? "shown" : "hidden", rule_.warping());
    return grab;
}

void PointerGrabWatcher::put_at(std::int32_t screen_x, std::int32_t screen_y) {
    rule_.put_at({screen_x, screen_y}, ::GetTickCount64());
}

void PointerGrabWatcher::nudging() { rule_.nudged(cursor_now(), ::GetTickCount64()); }

}  // namespace rwin::guest
