#pragma once

#include "host/geometry/size.hpp"

#include <optional>

namespace rwin::host {

class GuestWindow;
struct MirrorRole;

[[nodiscard]] std::optional<Size> blank_picture(const GuestWindow& window, const MirrorRole& role, Size natural,
                                                bool pictured) noexcept;

}
