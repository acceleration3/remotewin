#pragma once

#include "host/geometry/size.hpp"
#include "host/link/guest_window.hpp"
#include "proto/min_max.hpp"

#include <optional>

namespace rwin::host {
struct PictureLimits {
    Size smallest;
    Size largest;

    [[nodiscard]] bool operator==(const PictureLimits&) const noexcept = default;

    [[nodiscard]] static PictureLimits of(const std::optional<MinMax>& limits, const GuestWindow::Margins& margins,
                                          Size padding, Size work_area) noexcept;
};

}
