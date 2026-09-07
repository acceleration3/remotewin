#include "guest/session/guest_session.hpp"

#include "guest/cursor/cursor_image.hpp"

#include <optional>
#include <string>
#include "guest/windows/window_surface.hpp"
#include "logging/logger.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

namespace rwin::guest {

GuestSession::GuestSession(TcpClient& link, std::uint32_t frames_per_second)
    : link_(link),
      frame_interval_ms_(1000 / (frames_per_second != 0 ? frames_per_second : 1)) {}

void GuestSession::run() {
    RWIN_INFO("session", "serving; frame interval {} ms", frame_interval_ms_);
    while (running_) {
        if (link_.wait_readable(frame_interval_ms_)) {
            if (!link_.receive(incoming_)) return;
            decoder_.feed(incoming_.data(), incoming_.size());
            incoming_.clear();

            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            while (decoder_.next(&payload, &size)) {
                MessageReader message{payload, size};
                handle(message);
            }
            if (decoder_.overflowed()) return;
        }
        push_window_changes();
        report_drags();
        send_frames();
        report_cursor();
    }
}

void GuestSession::handle(MessageReader& message) {
    switch (message.id()) {
        case MessageId::WatchScope: {
            const std::uint32_t process_id = message.u32();
            const std::string job = message.string();
            if (!message.ok()) break;
            RWIN_INFO("scope", "asked to watch pid {} job '{}'", process_id, job);
            watcher_ = std::make_unique<WindowListWatcher>(job, process_id);
            push_window_changes();
            break;
        }
        case MessageId::Subscribe: {
            const std::uint64_t window = message.u64();
            if (!message.ok()) break;
            captures_.try_emplace(window, std::make_unique<WindowCapture>(graphics_));
            RWIN_INFO("session", "subscribed to window {:#x} ({} now)", window,
                      captures_.size());
            break;
        }
        case MessageId::Unsubscribe: {
            const std::uint64_t window = message.u64();
            captures_.erase(window);
            drawn_.erase(window);
            RWIN_INFO("session", "unsubscribed from window {:#x}", window);
            break;
        }
        case MessageId::PointerMove: {
            const std::uint64_t window = message.u64();
            const std::int32_t x = message.i32();
            const std::int32_t y = message.i32();
            if (!message.ok()) break;
            const auto [into_x, into_y] = picture_offset(window);
            pointer_.move_to(WindowSurface{window}, x + into_x, y + into_y);
            break;
        }
        case MessageId::PointerButton: {
            const std::uint64_t window = message.u64();
            const std::int32_t x = message.i32();
            const std::int32_t y = message.i32();
            const auto button = static_cast<PointerInjector::Button>(message.u8());
            const bool pressed = message.u8() != 0;
            if (!message.ok()) break;
            const auto [into_x, into_y] = picture_offset(window);
            pointer_.press(WindowSurface{window}, x + into_x, y + into_y, button, pressed);
            break;
        }
        case MessageId::Key: {
            const std::uint64_t window = message.u64();
            const std::uint32_t virtual_key = message.u32();
            const std::uint32_t character = message.u32();
            const bool pressed = message.u8() != 0;
            if (!message.ok()) break;
            RWIN_DEBUG("input", "key {} vk={:#x} char={:#x}", pressed ? "down" : "up",
                       virtual_key, character);
            if (character != 0) {
                if (pressed) keyboard_.type(window, character);
            } else {
                keyboard_.press(window, virtual_key, pressed);
            }
            break;
        }
        case MessageId::FocusWindow: {
            const std::uint64_t window = message.u64();
            if (message.ok()) focus_.give_to(window);
            break;
        }
        case MessageId::CloseWindow: {
            const std::uint64_t window = message.u64();
            if (!message.ok()) break;
            RWIN_INFO("session", "closing window {:#x}", window);
            ::PostMessageW(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window)),
                           WM_CLOSE, 0, 0);
            break;
        }
        case MessageId::ResizeWindow: {
            const std::uint64_t window = message.u64();
            const std::uint32_t width = message.u32();
            const std::uint32_t height = message.u32();
            if (message.ok()) resizer_.resize(window, width, height);
            break;
        }
        default:
            RWIN_WARN("session", "unknown message id {}",
                      static_cast<int>(message.id()));
            break;
    }
}

void GuestSession::report_drags() {
    for (const std::uint64_t window : drags_.drained()) {
        MessageWriter out{MessageId::DragStarted};
        out.put_u64(window);
        if (!link_.send(out.take())) {
            running_ = false;
            return;
        }
    }
}

void GuestSession::report_cursor() {
    void* cursor = cursor_.changed_cursor();
    if (cursor == nullptr) return;
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(cursor));

    if (sent_cursors_.insert(id).second) {
        const CursorImage image{cursor};
        if (!image.valid()) {
            RWIN_WARN("cursor", "cursor {:#x} could not be decoded", id);
            sent_cursors_.erase(id);
            return;
        }
        RWIN_DEBUG("cursor", "new shape {:#x}: {}x{} hotspot {},{}", id, image.width(),
                   image.height(), image.hotspot_x(), image.hotspot_y());
        MessageWriter shape{MessageId::CursorShape};
        shape.put_u64(id);
        shape.put_u32(image.width());
        shape.put_u32(image.height());
        shape.put_u32(image.hotspot_x());
        shape.put_u32(image.hotspot_y());
        shape.put_bytes(image.pixels().data(), image.pixels().size());
        if (!link_.send(shape.take())) {
            running_ = false;
            return;
        }
    }

    MessageWriter changed{MessageId::CursorChanged};
    changed.put_u64(id);
    running_ = link_.send(changed.take());
}

void GuestSession::push_window_changes() {
    if (watcher_ == nullptr) return;
    std::optional<std::vector<GuestWindow>> changed = watcher_->changed();
    if (!changed.has_value()) return;
    RWIN_DEBUG("windows", "{} window(s)", changed->size());
    for (const GuestWindow& window : *changed) {
        RWIN_DEBUG("windows", "  {:#x} owner={:#x} {}x{} at {},{} class='{}' title='{}'",
                   window.handle, window.owner, window.width, window.height, window.x,
                   window.y, window.class_name, window.title);
    }
    send_window_list(*changed);
}

std::pair<std::int32_t, std::int32_t> GuestSession::picture_offset(
    std::uint64_t window_handle) const {
    const auto area = drawn_.find(window_handle);
    if (area == drawn_.end()) return {0, 0};
    return {static_cast<std::int32_t>(area->second.offset_x),
            static_cast<std::int32_t>(area->second.offset_y)};
}

void GuestSession::send_window_list(const std::vector<GuestWindow>& windows) {
    MessageWriter out{MessageId::WindowList};
    out.put_u8(watcher_->application_alive() ? 1 : 0);
    // What the guest has room for, so a mirror is not dragged past a size
    // the window could never be given.
    out.put_u32(room_.width());
    out.put_u32(room_.height());
    out.put_u32(static_cast<std::uint32_t>(windows.size()));
    for (const GuestWindow& window : windows) {
        // Reported as what it draws, not as the rectangle it owns.
        std::int32_t x = window.x;
        std::int32_t y = window.y;
        std::uint32_t width = window.width;
        std::uint32_t height = window.height;
        std::int32_t drawn_x = 0;
        std::int32_t drawn_y = 0;
        if (const auto area = drawn_.find(window.handle); area != drawn_.end()) {
            drawn_x = static_cast<std::int32_t>(area->second.offset_x);
            drawn_y = static_cast<std::int32_t>(area->second.offset_y);
            x += drawn_x;
            y += drawn_y;
            width = area->second.width;
            height = area->second.height;
        }

        out.put_u64(window.handle);
        out.put_u64(window.owner);
        out.put_u32(window.process_id);
        out.put_i32(x);
        out.put_i32(y);
        out.put_u32(width);
        out.put_u32(height);
        out.put_u32(window.style);
        out.put_u32(window.extended_style);
        out.put_u32(window.class_style);
        out.put_string(window.title);
        out.put_string(window.class_name);
        out.put_u8(static_cast<std::uint8_t>((window.minimized ? 1 : 0)
                                             | (window.maximized ? 2 : 0)
                                             | (window.dwm_frame ? 4 : 0)
                                             | (window.foreground ? 8 : 0)));
        out.put_u32(window.layered_flags);
        out.put_u8(window.layered_alpha);
        out.put_u32(window.color_key);
        out.put_u32(window.band);
        // The region is in the window's own coordinates; the host's
        // window begins where the drawn area does.
        out.put_u32(static_cast<std::uint32_t>(window.region.size()));
        for (const WindowRect& rect : window.region) {
            out.put_i32(rect.left - drawn_x);
            out.put_i32(rect.top - drawn_y);
            out.put_i32(rect.right - drawn_x);
            out.put_i32(rect.bottom - drawn_y);
        }
    }
    running_ = link_.send(out.take());
}

void GuestSession::send_frames() {
    std::vector<std::uint64_t> vanished;
    for (auto& [handle, capture] : captures_) {
        const WindowSurface surface{handle};
        if (surface.minimized()) continue;
        const WindowCapture::Result grabbed = capture->grab(surface);
        if (grabbed == WindowCapture::Result::Gone) {
            RWIN_INFO("frames", "window {:#x} can no longer be captured; dropping",
                      handle);
            vanished.push_back(handle);
            continue;
        }
        // Nothing new to send. The compositor produces a frame only when
        // it composed one, so a window sitting still simply has no news.
        if (grabbed == WindowCapture::Result::Unchanged) continue;

        const DrawnArea area{capture->offset_x(), capture->offset_y(), capture->width(),
                             capture->height()};
        auto [known, added] = drawn_.try_emplace(handle, area);
        const bool moved = added || known->second.offset_x != area.offset_x
                           || known->second.offset_y != area.offset_y
                           || known->second.width != area.width
                           || known->second.height != area.height;
        if (moved) {
            known->second = area;
            // The reported geometry is derived from this, so it has to be
            // said again now that it has changed.
            if (watcher_ != nullptr) watcher_->forget();
        }
        MessageWriter out{MessageId::Frame};
        out.put_u64(handle);
        out.put_u32(capture->width());
        out.put_u32(capture->height());
        out.put_bytes(capture->pixels(), capture->size_bytes());
        if (!link_.send(out.take())) {
            running_ = false;
            return;
        }
    }
    for (const std::uint64_t handle : vanished) {
        captures_.erase(handle);
        drawn_.erase(handle);
        send_window_gone(handle);
    }
}

void GuestSession::send_window_gone(std::uint64_t window_handle) {
    MessageWriter out{MessageId::WindowGone};
    out.put_u64(window_handle);
    running_ = link_.send(out.take());
}

}  // namespace rwin::guest
