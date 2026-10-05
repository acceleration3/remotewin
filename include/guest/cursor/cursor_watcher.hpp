#pragma once

#include "proto/guest_cursor.hpp"

#include <optional>

namespace rwin::guest {

// Notices that the guest's cursor became a different one, or was hidden or
// shown.
//
// There is no notification for this: a cursor is set by whichever window
// the pointer is over, whenever it likes, and the only way to learn about
// it is to look. So this is asked once per frame, and answers only when
// what it sees is not what it answered with last.
class CursorWatcher {
public:
    // The cursor now - its handle as the id, which is stable for a shape
    // and lets the host keep the pictures - or that none is shown: hidden
    // by ShowCursor, set to nothing by SetCursor(NULL), or suppressed for
    // touch and pen.
    [[nodiscard]] std::optional<GuestCursor> changed();

private:
    std::optional<GuestCursor> last_;
};

}  // namespace rwin::guest
