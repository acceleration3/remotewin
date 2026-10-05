#include "host/cursor/emulated_cursor.hpp"

#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"

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
        // Copied: the bytes belong to the message.
        const QImage image(frame.bgra.data(), static_cast<int>(frame.width), static_cast<int>(frame.height),
                           static_cast<int>(frame.width * 4), QImage::Format_ARGB32);
        look.frames.push_back(CursorLook::Frame{image.copy(),
                                                QPoint(static_cast<int>(frame.hotspot_x), static_cast<int>(frame.hotspot_y)),
                                                static_cast<int>(frame.delay_ms)});
    }
    if (look.frames.empty()) {
        RWIN_WARN("cursor", "cursor {:#x} arrived with no picture", picture.id);
        return;
    }
    RWIN_DEBUG("cursor", "remembered {:#x} {} ({} picture(s), {}x{})", picture.id, name_of(picture.system),
               look.frames.size(), look.frames.front().image.width(), look.frames.front().image.height());
    looks_.insert_or_assign(picture.id, std::move(look));
}

const CursorLook* EmulatedCursor::find(std::uint64_t id) const {
    const auto found = looks_.find(id);
    return found == looks_.end() ? nullptr : &found->second;
}

}  // namespace rwin::host
