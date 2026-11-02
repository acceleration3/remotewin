#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"

#include <optional>

namespace rwin::host {
struct UnitPadding {
    Point offset;
    Size padded;
    Size logical;
    Point logical_hotspot;

    [[nodiscard]] bool operator==(const UnitPadding&) const noexcept = default;
};

[[nodiscard]] std::optional<UnitPadding> unit_padding(Size picture, Point hotspot, double ratio);

}
