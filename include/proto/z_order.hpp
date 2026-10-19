#pragma once

#include "proto/window_id.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

// The guest's stacking of the windows it reports, topmost first, and its
// active window (RAIL's Desktop order with ZORDER and ACTIVEWND): sent when
// either changes, once the guest has confirmed the change took. The
// guest's marker window is in the list as kMonitorWindow's id, {0, 0}: a
// window the host is told is active is followed only if it is above it.
// ``active`` is none when nothing reported is active, the marker included.
struct ZOrder {
    // RAIL's 256 windows, and the marker.
    static constexpr std::size_t kMaxWindows = 257;

    WindowId active;
    std::vector<WindowId> windows;

    // Whether ``window`` is above the marker in ``windows``; false for a
    // window not listed, or a list with no marker.
    [[nodiscard]] bool above_marker(WindowId window) const noexcept;

    [[nodiscard]] bool operator==(const ZOrder&) const = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ZOrder> read(MessageReader& in);
};

}  // namespace rwin
