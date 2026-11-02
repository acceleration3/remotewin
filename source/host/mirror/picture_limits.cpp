#include "host/mirror/picture_limits.hpp"

#include <algorithm>

namespace rwin::host {

namespace {
int tightest(int first, int second) noexcept {
    if (first <= 0) return std::max(0, second);
    if (second <= 0) return first;
    return std::min(first, second);
}

}

PictureLimits PictureLimits::of(const std::optional<MinMax>& limits, const GuestWindow::Margins& margins,
                                Size padding, Size work_area) noexcept {
    const Size borders{margins.left + margins.right, margins.top + margins.bottom};
    const Size room{work_area.width > 0 ? work_area.width - padding.width : 0,
                    work_area.height > 0 ? work_area.height - padding.height : 0};
    PictureLimits picture{Size{}, room};
    if (limits.has_value()) {
        picture.smallest = Size{std::max(0, limits->min_track_width - borders.width - padding.width),
                                std::max(0, limits->min_track_height - borders.height - padding.height)};
        const Size track{limits->max_track_width > 0 ? limits->max_track_width - borders.width - padding.width : 0,
                         limits->max_track_height > 0 ? limits->max_track_height - borders.height - padding.height
                                                      : 0};
        picture.largest = Size{tightest(room.width, track.width), tightest(room.height, track.height)};
    }
    picture.largest = Size{std::max(0, picture.largest.width), std::max(0, picture.largest.height)};
    if (picture.largest.width > 0) picture.largest.width = std::max(picture.largest.width, picture.smallest.width);
    if (picture.largest.height > 0) {
        picture.largest.height = std::max(picture.largest.height, picture.smallest.height);
    }
    return picture;
}

}
