#pragma once

#include <cstdint>

namespace rwin::guest {

// Notices that the guest's cursor became a different one.
//
// There is no notification for this: a cursor is set by whichever window
// the pointer is over, whenever it likes, and the only way to learn about
// it is to look. So this is asked once per frame, and answers with the
// handle only when it is not the handle it answered with last.
class CursorWatcher {
public:
    // The new cursor's handle, or nullptr when it has not changed. The
    // handle is stable for a given shape, which is what lets the host
    // cache the pixels rather than be sent them every frame.
    [[nodiscard]] void* changed_cursor();

private:
    void* last_ = nullptr;
};

}  // namespace rwin::guest
