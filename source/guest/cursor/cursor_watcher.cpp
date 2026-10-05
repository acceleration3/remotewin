#include "guest/cursor/cursor_watcher.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

std::optional<GuestCursor> CursorWatcher::changed() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) == FALSE) return std::nullopt;

    const bool shown = (info.flags & CURSOR_SHOWING) != 0 && (info.flags & CURSOR_SUPPRESSED) == 0
                       && info.hCursor != nullptr;
    // A hidden cursor's handle is whichever was last set, and says nothing
    // the host shows.
    const GuestCursor now{shown ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info.hCursor)) : 0,
                          shown};
    if (last_ == now) return std::nullopt;
    last_ = now;
    return now;
}

}  // namespace rwin::guest
