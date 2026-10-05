#pragma once

#include "host/cursor/cursor_look.hpp"

#include <cstdint>
#include <unordered_map>

namespace rwin {
struct CursorPicture;
}

namespace rwin::host {

// The guest's cursors, as they have been sent: once each, then named.
//
// A pointer moving between a text field and a button does not resend two
// bitmaps thirty times a second. Kept for the session's life, at a stable
// place each (a look handed out stays valid).
class EmulatedCursor {
public:
    void remember(const CursorPicture& picture);

    // Null when the cursor has not arrived yet, which happens only if the
    // guest names one before sending it.
    [[nodiscard]] const CursorLook* find(std::uint64_t id) const;

private:
    std::unordered_map<std::uint64_t, CursorLook> looks_;
};

}  // namespace rwin::host
