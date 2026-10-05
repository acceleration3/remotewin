#include "host/cursor/cursor_player.hpp"

#include "host/cursor/cursor_look.hpp"
#include "host/cursor/native_cursor_shape.hpp"
#include "logging/logger.hpp"

#include <utility>
#include <vector>

namespace rwin::host {

CursorPlayer::CursorPlayer(QObject* parent) : QObject(parent) {
    connect(&animation_, &CursorAnimation::stepped, this, &CursorPlayer::step_to);
}

void CursorPlayer::remember(const CursorPicture& picture) { looks_.remember(picture); }

const CursorLook* CursorPlayer::look() const {
    return shown_.shown ? looks_.find(shown_.id) : nullptr;
}

void CursorPlayer::show(const GuestCursor& cursor) {
    const CursorLook* look = cursor.shown ? looks_.find(cursor.id) : nullptr;
    if (cursor.shown && look == nullptr) {
        RWIN_DEBUG("cursor", "cursor {:#x} named before it arrived", cursor.id);
        return;
    }
    shown_ = cursor;
    std::vector<int> delays;
    if (look != nullptr && look->animated() && !native_shape_for(look->system).has_value()) {
        delays.reserve(look->frames.size());
        for (const CursorLook::Frame& frame : look->frames) delays.push_back(frame.delay_ms);
    }
    animation_.play(std::move(delays));
    Q_EMIT worn(look, 0);
}

void CursorPlayer::step_to(std::size_t step) {
    const CursorLook* shown = look();
    if (shown != nullptr) Q_EMIT worn(shown, step);
}

}  // namespace rwin::host
