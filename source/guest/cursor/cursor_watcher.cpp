#include "guest/cursor/cursor_watcher.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

void* CursorWatcher::changed_cursor() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) == FALSE) return nullptr;

    // A hidden cursor reports no handle. That is a change worth passing
    // on, but there is nothing to decode, so it is not reported here —
    // the session sees the same handle and leaves the host's cursor be.
    if (info.hCursor == nullptr || info.hCursor == last_) return nullptr;
    last_ = info.hCursor;
    return info.hCursor;
}

}  // namespace rwin::guest
