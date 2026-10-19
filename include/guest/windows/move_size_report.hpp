#pragma once

#include "proto/move_size_kind.hpp"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace rwin::guest {

class Outbox;
class WindowTracker;

// Tells the host that Windows began or ended moving or resizing a window,
// and which way (rdpshell's OnHookEventMoveSizeStartEx and
// OnHookEventMoveSizeEnd, docs/rail-model.md §2.5).
//
// win32k's move/size loop raises the undocumented event 0x19 right after
// EVENT_SYSTEM_MOVESIZESTART, its idChild the loop's kind: 0 keyboard size,
// 1-8 the edges in WMSZ order, 9 move, 10 keyboard move. On it the host is
// sent the window's MinMax and then MoveSizeStart; it takes the gesture
// over with its own compositor and lets the press go, which ends the loop
// having moved nothing. Every window, owned or not.
//
// Processes named in LocalMoveSizeIgnoreList - Windows' own list, read as
// rdpshell reads it: value names, file names only, compared as they are -
// keep Windows' loop: an application that docks panels by watching its
// window move needs the loop to run.
class MoveSizeReport {
public:
    MoveSizeReport(WindowTracker& tracker, Outbox& outbox);

    void started(void* window, long kind);
    void ended(void* window);

private:
    [[nodiscard]] bool ignored(void* window) const;

    WindowTracker& tracker_;
    Outbox& outbox_;
    std::set<std::wstring> ignored_;
    std::unordered_map<void*, MoveSizeKind> moving_;
};

}  // namespace rwin::guest
