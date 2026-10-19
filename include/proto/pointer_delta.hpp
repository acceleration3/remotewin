#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The held pointer's own motion, in counts: what a mouse reports, before
// any acceleration. The host sends it while the compositor holds the
// pointer locked to a mirror; the guest moves its cursor by it as a mouse
// would (relative SendInput), which is what reaches raw input.
struct PointerDelta {
    // The guest window the pointer is held on.
    WindowId window;
    std::int32_t dx = 0;
    std::int32_t dy = 0;

    [[nodiscard]] bool operator==(const PointerDelta&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerDelta> read(MessageReader& in);
};

}  // namespace rwin
