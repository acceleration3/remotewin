#pragma once

#include "host/export/unique_fd.hpp"
#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"
#include "host/link/guest_windows.hpp"
#include "host/link/pixels_view.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "proto/clock_echo.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/feature_set.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/message_id.hpp"
#include "proto/move_size_end.hpp"
#include "proto/move_size_start.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/rect.hpp"
#include "proto/slot_ring.hpp"
#include "proto/sys_command.hpp"
#include "proto/window_icon.hpp"
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rwin {
struct KeyStroke;
class MessageReader;
struct PointerAt;
struct PointerWheel;
struct SlotFree;
struct SysParam;
}

namespace rwin::host {
class GuestLink {
public:
    explicit GuestLink(EventLoop& loop);
    ~GuestLink();

    GuestLink(const GuestLink&) = delete;
    GuestLink& operator=(const GuestLink&) = delete;
    GuestLink(GuestLink&&) = delete;
    GuestLink& operator=(GuestLink&&) = delete;

    [[nodiscard]] bool listen(std::uint16_t port, bool loopback_only);
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    [[nodiscard]] const FeatureSet& features() const noexcept { return features_; }
    [[nodiscard]] const GuestWindows& windows() const noexcept { return windows_; }

    void watch_monitor();
    void subscribe(WindowId window);
    void unsubscribe(WindowId window);
    void send_activate(WindowId window, bool enabled);
    void send_sys_command(WindowId window, SysCommand::Command command);
    void send_sys_menu(WindowId window, std::int32_t x, std::int32_t y);
    void send_window_move(WindowId window, const Rect& frame);
    void send_sys_param(const SysParam& param);
    void send_pointer_move(WindowId window, Point at);
    void send_pointer_button(WindowId window, Point at, std::uint8_t button, bool pressed);
    void send_pointer_delta(WindowId window, std::int32_t dx, std::int32_t dy);
    void send_pointer_wheel(const PointerWheel& wheel);
    void send_pointer_at(const PointerAt& at);
    void send_pointer_leave(WindowId window);
    void send_key(const KeyStroke& stroke);
    void send_popup_dismiss(WindowId window);
    void send_route(bool shared_surfaces);
    void send_window_pixels(WindowId window);
    void send_slot_free(const SlotFree& freed);
    void send_clock_probe();

    Signal<> connected;
    Signal<const std::string&> mismatched;
    Signal<> disconnected;

    Signal<> sync_began;
    Signal<> sync_ended;
    Signal<WindowId> window_new;
    Signal<WindowId, std::uint32_t> window_changed;
    Signal<WindowId> window_gone;
    Signal<const ZOrder&> restacked;
    Signal<WindowId> window_limited;
    Signal<const MoveSizeStart&> move_size_started;
    Signal<const MoveSizeEnd&> move_size_ended;
    Signal<const WindowIcon&> window_icon;
    Signal<const Box&> work_area_changed;
    Signal<const Box&> monitor_changed;

    Signal<WindowId, const PixelsView&> frame_arrived;
    Signal<WindowId, Size, Point, const PixelsView&> frame_patched;
    Signal<WindowId> frame_patches_done;
    Signal<const SlotRing&> ring_announced;
    Signal<const FrameInSlot&> frame_in_slot;
    Signal<WindowId> surface_lost;
    Signal<const CursorPicture&> cursor_pictured;
    Signal<const GuestCursor&> cursor_changed;
    Signal<const PointerGrab&> pointer_grabbed;
    Signal<const ClockEcho&, std::int64_t> clock_echoed;

private:
    void accept_guest();
    void read_guest();
    void write_pending();
    void drop_guest();
    void greet(const std::uint8_t* payload, std::size_t size);
    void refuse(const std::string& reason);
    void dispatch(const std::uint8_t* payload, std::size_t size);
    [[nodiscard]] bool dispatch_windows(MessageReader& message);
    [[nodiscard]] bool dispatch_frames(MessageReader& message);
    [[nodiscard]] bool dispatch_pointer(MessageReader& message);
    void read_window_new(MessageReader& message);
    void read_window_changed(MessageReader& message);
    void read_window_gone(MessageReader& message);
    void read_sync_end();
    void read_frame(MessageReader& message);
    void read_patch(MessageReader& message);
    void read_sys_param(MessageReader& message);
    void read_icon(MessageReader& message);
    void send(std::string_view feature, const std::vector<std::uint8_t>& bytes);
    void send_window(std::string_view feature, MessageId id, WindowId window);
    void write_now(const std::uint8_t* bytes, std::size_t size);

    EventLoop& loop_;
    UniqueFd listener_;
    std::unique_ptr<EventLoop::Watch> listening_;
    std::uint16_t port_ = 0;
    UniqueFd guest_;
    std::unique_ptr<EventLoop::Watch> guest_watch_;
    std::vector<std::uint8_t> unsent_;
    std::vector<std::uint8_t> chunk_;
    FrameDecoder decoder_;
    FeatureSet features_;
    GuestWindows windows_;
    bool greeted_ = false;
};

}
