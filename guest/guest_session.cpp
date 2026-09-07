#include "guest/guest_session.hpp"

#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

#include "log/logger.hpp"

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

namespace rwin::guest {

GuestSession::GuestSession(TcpClient& link, std::uint32_t frames_per_second)
    : link_(link), frame_interval_ms_(1000 / (frames_per_second != 0 ? frames_per_second : 1)) {}

void GuestSession::run() {
    RWIN_INFO("session", "serving; frame interval {} ms", frame_interval_ms_);
    while (running_) {
        if (link_.wait_readable(frame_interval_ms_)) {
            if (!link_.receive(incoming_)) {
                return;
            }
            decoder_.feed(incoming_.data(), incoming_.size());
            incoming_.clear();

            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            while (decoder_.next(&payload, &size)) {
                MessageReader message{payload, size};
                handle(message);
            }
            if (decoder_.overflowed()) {
                return;
            }
        }
        send_frames();
    }
}

void GuestSession::handle(MessageReader& message) {
    switch (message.id()) {
        case MessageId::ListWindows:
            send_window_list(message.u32());
            break;
        case MessageId::Subscribe: {
            const std::uint64_t window = message.u64();
            if (message.ok()) {
                captures_.try_emplace(window, std::make_unique<WindowCapture>());
                RWIN_INFO("session", "subscribed to window {:#x} ({} now)", window,
                          captures_.size());
            }
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
            if (message.ok()) input_.move(window, x, y);
            break;
        }
        case MessageId::PointerButton: {
            const std::uint64_t window = message.u64();
            const std::int32_t x = message.i32();
            const std::int32_t y = message.i32();
            const auto button = static_cast<InputInjector::Button>(message.u8());
            const bool pressed = message.u8() != 0;
            if (message.ok()) input_.button(window, x, y, button, pressed);
            break;
        }
        case MessageId::Key: {
            const std::uint64_t window = message.u64();
            const std::uint32_t virtual_key = message.u32();
            const std::uint32_t character = message.u32();
            const bool pressed = message.u8() != 0;
            if (message.ok()) input_.key(window, virtual_key, character, pressed);
            break;
        }
        case MessageId::CloseWindow: {
            const std::uint64_t window = message.u64();
            if (message.ok()) {
                ::PostMessageW(
                    reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window)),
                    WM_CLOSE, 0, 0);
            }
            break;
        }
        case MessageId::ResizeWindow: {
            const std::uint64_t window = message.u64();
            const std::uint32_t width = message.u32();
            const std::uint32_t height = message.u32();
            if (message.ok()) {
                ::SetWindowPos(
                    reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window)), nullptr,
                    0, 0, static_cast<int>(width), static_cast<int>(height),
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        }
        default:
            break;
    }
}

void GuestSession::send_window_list(std::uint32_t process_id) {
    const std::vector<GuestWindow> found = windows_.windows_of(process_id);
    RWIN_DEBUG("windows", "listing for pid {}: {} window(s)", process_id, found.size());
    for (const GuestWindow& window : found) {
        RWIN_DEBUG("windows", "  {:#x} pid={} {}x{} at {},{} style={:#010x} ex={:#010x} class='{}' title='{}'",
                   window.handle, window.process_id, window.width, window.height,
                   window.x, window.y, window.style, window.extended_style,
                   window.class_name, window.title);
    }

    MessageWriter out{MessageId::WindowList};
    out.put_u32(static_cast<std::uint32_t>(found.size()));
    for (const GuestWindow& window : found) {
        out.put_u64(window.handle);
        out.put_u32(window.process_id);
        out.put_i32(window.x);
        out.put_i32(window.y);
        out.put_u32(window.width);
        out.put_u32(window.height);
        out.put_u32(window.style);
        out.put_u32(window.extended_style);
        out.put_string(window.title);
        out.put_string(window.class_name);
    }
    running_ = link_.send(out.take());
}

void GuestSession::send_frames() {
    std::vector<std::uint64_t> vanished;
    for (auto& [handle, capture] : captures_) {
        if (!capture->grab(handle)) {
            RWIN_INFO("frames", "window {:#x} no longer captures; dropping", handle);
            vanished.push_back(handle);
            continue;
        }
        RWIN_TRACE("frames", "window {:#x} {}x{} ({} bytes)", handle, capture->width(),
                   capture->height(), capture->size_bytes());
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
