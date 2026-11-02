#include "guest/windows/window_icons.hpp"

#include "guest/cursor/cursor_image.hpp"
#include "guest/session/outbox.hpp"
#include "logging/logger.hpp"
#include "proto/message_writer.hpp"
#include "proto/window_icon.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {
constexpr UINT kAnswerMs = 50;

HICON asked_icon(HWND window, WPARAM kind) {
    DWORD_PTR icon = 0;
    if (::SendMessageTimeoutW(window, WM_GETICON, kind, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, kAnswerMs, &icon) == 0) {
        return nullptr;
    }
    return reinterpret_cast<HICON>(icon);
}

HICON icon_of(HWND window) {
    if (HICON icon = asked_icon(window, ICON_BIG)) return icon;
    if (HICON icon = asked_icon(window, ICON_SMALL2)) return icon;
    if (auto icon = reinterpret_cast<HICON>(::GetClassLongPtrW(window, GCLP_HICON))) return icon;
    return reinterpret_cast<HICON>(::GetClassLongPtrW(window, GCLP_HICONSM));
}

}

void WindowIcons::look(void* window, WindowId id) {
    HICON icon = icon_of(static_cast<HWND>(window));
    if (icon == nullptr) return;
    void*& sent = sent_[id.key()];
    if (sent == icon) return;
    CursorImage image{icon};
    if (!image.valid() || image.width() == 0 || image.height() == 0 || image.width() > WindowIcon::kMaxSide
        || image.height() > WindowIcon::kMaxSide) {
        RWIN_DEBUG("icons", "{}'s icon cannot be read", id);
        return;
    }
    sent = icon;
    WindowIcon message{id, WindowIcon::Size::Big, static_cast<std::uint16_t>(image.width()),
                       static_cast<std::uint16_t>(image.height()), image.take_pixels()};
    MessageWriter out{MessageId::WindowIcon};
    message.write(out);
    outbox_.send(out.take());
    RWIN_DEBUG("icons", "{}'s icon, {}x{}", id, message.width, message.height);
}

void WindowIcons::forget(WindowId id) { sent_.erase(id.key()); }

}
