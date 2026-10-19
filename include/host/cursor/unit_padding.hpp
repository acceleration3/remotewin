#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"

#include <optional>

namespace rwin::host {

// How a cursor picture in device pixels is padded with transparency so that
// at a ratio of p/q device pixels per logical unit its every edge and its
// hotspot fall on whole logical units: then a cursor surface of ``logical``
// units shows it one device pixel to one, with its hotspot exactly where the
// pointer is.
struct UnitPadding {
    // Where the picture goes in the padded one.
    Point offset;
    Size padded;
    Size logical;
    Point logical_hotspot;

    [[nodiscard]] bool operator==(const UnitPadding&) const noexcept = default;
};

// Nothing for a ratio that is no small fraction (a compositor's are 120ths
// at most) or whose numerator would pad a cursor by more than it is wide.
[[nodiscard]] std::optional<UnitPadding> unit_padding(Size picture, Point hotspot, double ratio);

}  // namespace rwin::host
