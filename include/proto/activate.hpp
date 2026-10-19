#pragma once

#include "proto/window_id.hpp"

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The host's focus moved to a mirror by some means other than a press in
// it - Alt+Tab, the taskbar, the compositor focusing a window it mapped -
// or left every mirror (RAIL's Client Activate). A press activates its
// window in the guest by itself, as injected input, and is not told.
//
// ``enabled``: make ``window`` the guest's foreground. Not enabled: focus
// left ``window`` for something that is not a mirror, and the guest makes
// its marker window the foreground, so nothing it shows is active - but
// only while ``window`` still is: a deactivation that arrives after a
// press elsewhere has moved the foreground on is stale.
struct Activate {
    WindowId window;
    bool enabled = true;

    [[nodiscard]] bool operator==(const Activate&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<Activate> read(MessageReader& in);
};

}  // namespace rwin
