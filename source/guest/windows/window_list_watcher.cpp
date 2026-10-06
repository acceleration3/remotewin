#include "guest/windows/window_list_watcher.hpp"

#include <algorithm>

namespace rwin::guest {

namespace {

bool same(const GuestWindow& a, const GuestWindow& b) {
    return a.handle == b.handle && a.owner == b.owner && a.x == b.x && a.y == b.y
           && a.width == b.width && a.height == b.height && a.style == b.style
           && a.extended_style == b.extended_style && a.class_style == b.class_style
           && a.title == b.title && a.minimized == b.minimized
           && a.maximized == b.maximized && a.dwm_frame == b.dwm_frame
           && a.layered_flags == b.layered_flags && a.layered_alpha == b.layered_alpha
           && a.color_key == b.color_key && a.band == b.band
           && a.foreground == b.foreground && a.region == b.region;
}

}  // namespace

std::optional<std::vector<GuestWindow>> WindowListWatcher::changed() {
    if (const auto handed = hand_off_.handed_to(scope_)) scope_.adopt(*handed);
    std::vector<GuestWindow> found = windows_.windows_in(scope_);
    const bool alive = scope_.alive();
    if (alive == alive_ && found.size() == known_.size()
        && std::equal(found.begin(), found.end(), known_.begin(), same)) {
        return std::nullopt;
    }
    alive_ = alive;
    known_ = found;
    return found;
}

}  // namespace rwin::guest
