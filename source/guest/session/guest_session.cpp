#include "guest/session/guest_session.hpp"

#include "guest/cursor/cursor_image.hpp"

#include <optional>
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
        case MessageId::ListWindows: {
            const std::uint32_t process_id = message.u32();
            if (!message.ok()) break;
            if (watcher_ == nullptr) {
                watcher_ = std::make_unique<WindowListWatcher>(process_id);
            }
            watcher_->forget();
            push_window_changes();
        report_drags();
            break;
        }
        case MessageId::Subscribe: {
            const std::uint64_t window = message.u64();
            if (!message.ok()) break;
            captures_.try_emplace(window, std::make_unique<WindowCapture>());
            RWIN_INFO("session", "subscribed to window {:#x} ({} now)", window,
                      captures_.size());
            break;
        }
        case MessageId::Unsubscribe: {
            const std::uint64_t window = message.u64();
            captures_.erase(window);
            RWIN_INFO("session", "unsubscribed from window {:#x}", window);
            break;
        }
        case MessageId::PointerMove: {
            const std::uint64_t window = message.u64();
            const std::int32_t x = message.i32();
            const std::int32_t y = message.i32();
            if (!message.ok()) break;
            pointer_.move_to(WindowSurface{window}, x, y);
            break;
        }
        case MessageId::PointerButton: {
            const std::uint64_t window = message.u64();
            const std::int32_t x = message.i32();
            const std::int32_t y = message.i32();
            const auto button = static_cast<PointerInjector::Button>(message.u8());
            const bool pressed = message.u8() != 0;
            if (!message.ok()) break;
            pointer_.press(WindowSurface{window}, x, y, button, pressed);
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

void GuestSession::send_window_list(const std::vector<GuestWindow>& windows) {
    MessageWriter out{MessageId::WindowList};
    out.put_u32(static_cast<std::uint32_t>(windows.size()));
    for (const GuestWindow& window : windows) {
        out.put_u64(window.handle);
        out.put_u64(window.owner);
        out.put_u32(window.process_id);
        out.put_i32(window.x);
        out.put_i32(window.y);
        out.put_u32(window.width);
        out.put_u32(window.height);
        out.put_u32(window.style);
        out.put_u32(window.extended_style);
        out.put_string(window.title);
        out.put_string(window.class_name);
        out.put_u8(static_cast<std::uint8_t>((window.minimized ? 1 : 0)
                                             | (window.maximized ? 2 : 0)));
    }
    running_ = link_.send(out.take());
}

void GuestSession::send_frames() {
    std::vector<std::uint64_t> vanished;
    for (auto& [handle, capture] : captures_) {
        const WindowSurface surface{handle};
        if (surface.minimized()) continue;
        if (!capture->grab(surface)) {
            RWIN_INFO("frames", "window {:#x} no longer captures; dropping", handle);
            vanished.push_back(handle);
            continue;
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
        send_window_gone(handle);
    }
}

void GuestSession::send_window_gone(std::uint64_t window_handle) {
    MessageWriter out{MessageId::WindowGone};
    out.put_u64(window_handle);
    running_ = link_.send(out.take());
}

}  // namespace rwin::guest
