#pragma once

#include "proto/rect.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// A desktop setting one end tells the other (RAIL's System Parameters),
// numbered as SystemParametersInfo numbers it, each with its own body.
//
//   WorkArea         guest to host: the work area of the guest's primary
//                    monitor, the room a window can have there.
//   DragFullWindows  host to guest: whether a window Windows drags is
//                    redrawn as it moves. The host asks for it on: off,
//                    Windows moves a window only at the end of a drag it
//                    keeps, drawing an outline on a desktop nobody sees.
struct SysParam {
    enum class Param : std::uint32_t { DragFullWindows = 0x0025, WorkArea = 0x002F };

    Param param = Param::WorkArea;
    Rect work_area;
    bool enabled = false;

    [[nodiscard]] bool operator==(const SysParam&) const noexcept = default;

    void write(MessageWriter& out) const;
    // Refused for a setting not listed above.
    [[nodiscard]] static std::optional<SysParam> read(MessageReader& in);
};

}  // namespace rwin
