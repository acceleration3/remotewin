#include "host/mirror/settled_room.hpp"

#include "host/geometry/view_mapping.hpp"

namespace rwin::host {

std::optional<Size> SettledRoom::changed(const ToplevelConfigure& configure, double scale) noexcept {
    if (configure.resizing || configure.size.empty() || scale <= 0.0) return std::nullopt;
    const Size pixels = ViewMapping::device_size(configure.size, scale);
    if (pixels == told_) return std::nullopt;
    told_ = pixels;
    return pixels;
}

}
