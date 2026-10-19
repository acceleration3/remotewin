#pragma once

#include <cstdint>

namespace rwin::host {

// A cursor this desktop draws by name: wp_cursor_shape_device_v1's shapes,
// which are the CSS cursor names, numbered as that protocol numbers them.
enum class CursorShape : std::uint32_t {
    Default = 1,
    Help = 3,
    Pointer = 4,
    Progress = 5,
    Wait = 6,
    Crosshair = 8,
    Text = 9,
    NotAllowed = 15,
    EResize = 18,
    NResize = 19,
    NeResize = 20,
    NwResize = 21,
    SResize = 22,
    SeResize = 23,
    SwResize = 24,
    WResize = 25,
    EwResize = 26,
    NsResize = 27,
    NeswResize = 28,
    NwseResize = 29,
    AllScroll = 32,
};

}  // namespace rwin::host
