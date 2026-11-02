#include "host/placement/output_layout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rwin::host {

void OutputLayout::replace(std::vector<Output> outputs) { outputs_ = std::move(outputs); }

void OutputLayout::prefer(std::string name) { preferred_ = std::move(name); }

const OutputLayout::Output& OutputLayout::guest_output() const {
    const auto named = std::find_if(outputs_.begin(), outputs_.end(),
                                    [this](const Output& output) { return output.name == preferred_; });
    return named != outputs_.end() ? *named : outputs_.front();
}

const OutputLayout::Output& OutputLayout::output_of(const BoxF& frame) const {
    const double x = frame.x + frame.width / 2;
    const double y = frame.y + frame.height / 2;
    const auto holding = std::find_if(outputs_.begin(), outputs_.end(), [&](const Output& output) {
        return x >= output.origin.x && x < output.origin.x + output.size.x && y >= output.origin.y
               && y < output.origin.y + output.size.y;
    });
    return holding != outputs_.end() ? *holding : guest_output();
}

Point OutputLayout::guest_point(const BoxF& frame) const {
    const Output& on = output_of(frame);
    const Output& guest = guest_output();
    const Point raw = mapping_of(on).guest_point(frame.corner());
    const auto pixels = [](double logical, double scale) { return static_cast<int>(std::lround(logical * scale)); };
    const int room_x = std::max(0, pixels(guest.size.x, guest.scale) - pixels(frame.width, on.scale));
    const int room_y = std::max(0, pixels(guest.size.y, guest.scale) - pixels(frame.height, on.scale));
    return Point{std::clamp(raw.x, 0, room_x), std::clamp(raw.y, 0, room_y)};
}

PointF OutputLayout::host_point(Point guest, const std::optional<BoxF>& on) const {
    return mapping_of(on.has_value() ? output_of(*on) : guest_output()).host_point(guest);
}

}
