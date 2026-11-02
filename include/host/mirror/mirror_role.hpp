#pragma once

#include "proto/window_id.hpp"

#include <cstdint>

namespace rwin::host {

class GuestWindow;
class GuestWindows;

struct MirrorRole {
    enum class Kind : std::uint8_t { Toplevel, Popup, Subsurface };

    Kind kind = Kind::Toplevel;
    WindowId parent;

    [[nodiscard]] bool operator==(const MirrorRole&) const noexcept = default;

    [[nodiscard]] static MirrorRole of(const GuestWindow& window, const GuestWindows& windows);
};

[[nodiscard]] const char* name_of(MirrorRole::Kind kind) noexcept;

}
