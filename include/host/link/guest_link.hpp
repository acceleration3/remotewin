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
}  // namespace rwin

namespace rwin::host {

// The connection to the guest, and the only thing that speaks the wire.
//
// The host listens and the guest dials in: reaching a guest port needs a
// forwarding rule declared before the VM boots, while the guest can
// always reach the host.
//
// Each end says first which features it speaks (Hello). Nothing the guest
// sends is read until its hello has named the ones a link needs; a guest
// half from another build is refused with what it lacks named, rather
// than its messages misread field by field. From then on a message is sent
// only when its feature is active on both ends.
//
// What the guest says about its windows is kept here (windows()), because
// a change to a window is read over what was known of it; the signals say
// what changed once it is kept.
class GuestLink {
public:
    explicit GuestLink(EventLoop& loop);
    ~GuestLink();

    GuestLink(const GuestLink&) = delete;
    GuestLink& operator=(const GuestLink&) = delete;
    GuestLink(GuestLink&&) = delete;
    GuestLink& operator=(GuestLink&&) = delete;

    // On the loopback only, or on every IPv4 address: QEMU's user
    // networking forwards a guest's connection to 10.0.2.2 onto the
    // host's IPv4 loopback, and a v6-only bind refuses it with no hint as
    // to why. Port 0 is any free one (port()).
    [[nodiscard]] bool listen(std::uint16_t port, bool loopback_only);
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    [[nodiscard]] const FeatureSet& features() const noexcept { return features_; }
    [[nodiscard]] const GuestWindows& windows() const noexcept { return windows_; }

    // The guest's whole primary monitor, instead of its windows.
    void watch_monitor();
    void subscribe(WindowId window);
    void unsubscribe(WindowId window);
    void send_activate(WindowId window, bool enabled);
    void send_sys_command(WindowId window, SysCommand::Command command);
    // ``x``, ``y`` relative to the window's frame.
    void send_sys_menu(WindowId window, std::int32_t x, std::int32_t y);
    // The window's frame, on the guest's screen.
    void send_window_move(WindowId window, const Rect& frame);
    void send_sys_param(const SysParam& param);
    // At a point of ``window``'s picture.
    void send_pointer_move(WindowId window, Point at);
    void send_pointer_button(WindowId window, Point at, std::uint8_t button, bool pressed);
    // The held pointer's own motion, in counts.
    void send_pointer_delta(WindowId window, std::int32_t dx, std::int32_t dy);
    void send_pointer_wheel(const PointerWheel& wheel);
    void send_pointer_at(const PointerAt& at);
    // The pointer left the mirror of ``window`` for something that is not
    // the guest's.
    void send_pointer_leave(WindowId window);
    void send_key(const KeyStroke& stroke);
    // The compositor dismissed the popup that mirrors ``window``.
    void send_popup_dismiss(WindowId window);
    // Whether the guest is to keep its frames on the GPU (FrameRoute).
    void send_route(bool shared_surfaces);
    // One window's frames as pixels from here on (WindowPixels).
    void send_window_pixels(WindowId window);
    void send_slot_free(const SlotFree& freed);
    // This side's clock now, for the guest to answer (ClockEcho).
    void send_clock_probe();

    // A guest dialled in and speaks what a mirror needs (features()).
    Signal<> connected;
    // A guest dialled in that speaks too little of this wire, or another
    // wire: the reason says what it lacks. It has been disconnected.
    Signal<const std::string&> mismatched;
    Signal<> disconnected;

    // The guest is saying every window afresh; at its end, every window it
    // did not say again has been said gone.
    Signal<> sync_began;
    Signal<> sync_ended;
    Signal<WindowId> window_new;
    // The WindowFields that changed.
    Signal<WindowId, std::uint32_t> window_changed;
    Signal<WindowId> window_gone;
    Signal<const ZOrder&> restacked;
    // Windows' size limits for the window changed (GuestWindow::limits).
    Signal<WindowId> window_limited;
    Signal<const MoveSizeStart&> move_size_started;
    Signal<const MoveSizeEnd&> move_size_ended;
    Signal<const WindowIcon&> window_icon;
    // The work area of the guest's primary monitor, and the monitor whole,
    // in its pixels.
    Signal<const Box&> work_area_changed;
    Signal<const Box&> monitor_changed;

    Signal<WindowId, const PixelsView&> frame_arrived;
    // A part of ``window``'s picture, which is the size given, changed:
    // the pixels now go at the point given.
    Signal<WindowId, Size, Point, const PixelsView&> frame_patched;
    // Every part of one FramePatch has been told: the picture is whole.
    Signal<WindowId> frame_patches_done;
    // A window's frames are copied into this ring of shared surfaces from
    // here on (SlotRing).
    Signal<const SlotRing&> ring_announced;
    // A window's newest frame is in a slot of its ring (FrameInSlot).
    Signal<const FrameInSlot&> frame_in_slot;
    // A window's frames stopped, the window still there; they come again
    // by themselves.
    Signal<WindowId> surface_lost;
    Signal<const CursorPicture&> cursor_pictured;
    Signal<const GuestCursor&> cursor_changed;
    Signal<const PointerGrab&> pointer_grabbed;
    // The guest answered a clock probe; the second is this side's clock
    // (now_ns) as the answer was read.
    Signal<const ClockEcho&, std::int64_t> clock_echoed;

private:
    void accept_guest();
    void read_guest();
    void write_pending();
    void drop_guest();
    // The guest's first message, which has to be its hello.
    void greet(const std::uint8_t* payload, std::size_t size);
    void refuse(const std::string& reason);
    void dispatch(const std::uint8_t* payload, std::size_t size);
    // Each true when the message was one of its kind.
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
    // Sent when ``feature`` is active on the link; otherwise said in the
    // log and dropped.
    void send(std::string_view feature, const std::vector<std::uint8_t>& bytes);
    void send_window(std::string_view feature, MessageId id, WindowId window);
    void write_now(const std::uint8_t* bytes, std::size_t size);

    EventLoop& loop_;
    UniqueFd listener_;
    std::unique_ptr<EventLoop::Watch> listening_;
    std::uint16_t port_ = 0;
    UniqueFd guest_;
    std::unique_ptr<EventLoop::Watch> guest_watch_;
    // What the socket would not take yet, in order.
    std::vector<std::uint8_t> unsent_;
    std::vector<std::uint8_t> chunk_;
    FrameDecoder decoder_;
    FeatureSet features_;
    GuestWindows windows_;
    bool greeted_ = false;
};

}  // namespace rwin::host
