#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// A window's frames stopped: its capture failed or could not open. The
// window is still there - that is WindowGone's to say - and its frames
// come again once it can be captured, without being asked. For the
// monitor (kMonitorWindow) there is nothing to come back to.
struct SurfaceLost {
    enum class Reason : std::uint8_t { CaptureFailed = 0 };

    WindowId window;
    Reason reason = Reason::CaptureFailed;

    [[nodiscard]] bool operator==(const SurfaceLost&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SurfaceLost> read(MessageReader& in);
};

}  // namespace rwin
