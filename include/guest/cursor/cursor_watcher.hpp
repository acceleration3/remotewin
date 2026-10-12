#pragma once

#include "proto/guest_cursor.hpp"

#include <atomic>
#include <optional>
#include <thread>

namespace rwin::guest {

class FrameSignal;

// Notices that the guest's cursor became a different one, or was hidden or
// shown.
//
// A cursor is set by whichever window the pointer is over, whenever it
// likes. Windows says when the cursor's shape changes, or it is shown or
// hidden (EVENT_OBJECT_NAMECHANGE, _SHOW and _HIDE on OBJID_CURSOR): each
// time, ``changes`` is raised, so the session looks at once rather than at
// its next frame - a pointer over a still window waited up to a frame
// interval to change shape. What it sees is answered only when it is not
// what was answered last.
class CursorWatcher {
public:
    explicit CursorWatcher(const FrameSignal& changes);
    ~CursorWatcher();

    CursorWatcher(const CursorWatcher&) = delete;
    CursorWatcher& operator=(const CursorWatcher&) = delete;
    CursorWatcher(CursorWatcher&&) = delete;
    CursorWatcher& operator=(CursorWatcher&&) = delete;

    // The cursor now - its handle as the id, which is stable for a shape
    // and lets the host keep the pictures - or that none is shown: hidden
    // by ShowCursor, set to nothing by SetCursor(NULL), or suppressed for
    // touch and pen.
    [[nodiscard]] std::optional<GuestCursor> changed();

    // Called from the hook, on this watcher's own thread.
    void heard() const noexcept;

private:
    void listen();

    const FrameSignal& changes_;
    std::optional<GuestCursor> last_;
    std::thread worker_;
    std::atomic<unsigned long> thread_id_{0};
};

}  // namespace rwin::guest
