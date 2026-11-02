#include "host/cursor/emulated_cursor.hpp"

#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"

#include <algorithm>
#include <utility>

namespace rwin::host {

void EmulatedCursor::remember(const CursorPicture& picture) {
    CursorLook look;
    look.id = picture.id;
    look.system = picture.system;
    look.inverts = picture.inverts;
    look.frames.reserve(picture.frames.size());
    for (const CursorPicture::Frame& frame : picture.frames) {
        if (frame.width == 0 || frame.height == 0) continue;
        look.frames.push_back(CursorLook::Frame{Size{static_cast<int>(frame.width), static_cast<int>(frame.height)},
                                                frame.bgra,
                                                Point{static_cast<int>(frame.hotspot_x), static_cast<int>(frame.hotspot_y)},
                                                static_cast<int>(frame.delay_ms)});
    }
    if (look.frames.empty()) {
        RWIN_WARN("cursor", "cursor {:#x} arrived with no picture", picture.id);
        return;
    }
    RWIN_DEBUG("cursor", "remembered {:#x} {} ({} picture(s), {}x{})", picture.id, name_of(picture.system),
               look.frames.size(), look.frames.front().size.width, look.frames.front().size.height);
    if (!looks_.contains(picture.id)) make_room();
    looks_.insert_or_assign(picture.id, Kept{std::move(look), ++shows_});
}

void EmulatedCursor::make_room() {
    if (looks_.size() < kCapacity) return;
    const auto oldest = std::min_element(looks_.begin(), looks_.end(), [this](const auto& a, const auto& b) {
        if (a.first == worn_) return false;
        if (b.first == worn_) return true;
        return a.second.shown < b.second.shown;
    });
    RWIN_DEBUG("cursor", "let go of {:#x}, shown longest ago", oldest->first);
    looks_.erase(oldest);
}

const CursorLook* EmulatedCursor::show(std::uint64_t id) {
    const auto found = looks_.find(id);
    if (found == looks_.end()) return nullptr;
    found->second.shown = ++shows_;
    worn_ = id;
    return &found->second.look;
}

const CursorLook* EmulatedCursor::find(std::uint64_t id) const {
    const auto found = looks_.find(id);
    return found == looks_.end() ? nullptr : &found->second.look;
}

}
