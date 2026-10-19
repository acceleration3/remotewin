#pragma once

#include "host/link/guest_windows.hpp"
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
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

#include <QHostAddress>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <cstdint>
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

class QTcpServer;
class QTcpSocket;

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
class GuestLink : public QObject {
    Q_OBJECT
public:
    explicit GuestLink(QObject* parent = nullptr);

    [[nodiscard]] bool listen(std::uint16_t port, const QHostAddress& address = QHostAddress::AnyIPv4);
    [[nodiscard]] std::uint16_t port() const;

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
    void send_pointer_move(WindowId window, std::int32_t x, std::int32_t y);
    void send_pointer_button(WindowId window, std::int32_t x, std::int32_t y, std::uint8_t button, bool pressed);
    // The held pointer's own motion, in counts.
    void send_pointer_delta(WindowId window, std::int32_t dx, std::int32_t dy);
    void send_pointer_wheel(const PointerWheel& wheel);
    void send_pointer_at(const PointerAt& at);
    // The pointer left the mirror of ``window`` for something that is not
    // the guest's.
    void send_pointer_leave(WindowId window);
    void send_key(const KeyStroke& stroke);
    // Whether the guest is to keep its frames on the GPU (FrameRoute).
    void send_route(bool shared_surfaces);
    // One window's frames as pixels from here on (WindowPixels).
    void send_window_pixels(WindowId window);
    void send_slot_free(const SlotFree& freed);
    // This side's clock now, for the guest to answer (ClockEcho).
    void send_clock_probe();

Q_SIGNALS:
    // A guest dialled in and speaks what a mirror needs (features()).
    void connected();
    // A guest dialled in that speaks too little of this wire, or another
    // wire: ``reason`` says what it lacks. It has been disconnected.
    void mismatched(const QString& reason);
    void disconnected();

    // The guest is saying every window afresh; at its end, every window it
    // did not say again has been said gone.
    void sync_began();
    void sync_ended();
    void window_new(rwin::WindowId window);
    // ``mask``: the WindowFields that changed.
    void window_changed(rwin::WindowId window, quint32 mask);
    void window_gone(rwin::WindowId window);
    void restacked(const rwin::ZOrder& order);
    // Windows' size limits for the window changed (GuestWindow::limits).
    void window_limited(rwin::WindowId window);
    void move_size_started(const rwin::MoveSizeStart& start);
    void move_size_ended(const rwin::MoveSizeEnd& end);
    // The work area of the guest's primary monitor, in its pixels.
    void work_area_changed(QRect area);

    void frame_arrived(rwin::WindowId window, const QImage& pixels);
    // A part of ``window``'s picture, which is ``picture`` large, changed:
    // ``pixels`` now go at ``at``.
    void frame_patched(rwin::WindowId window, QSize picture, QPoint at, const QImage& pixels);
    // A window's frames are copied into this ring of shared surfaces from
    // here on (SlotRing).
    void ring_announced(const rwin::SlotRing& ring);
    // A window's newest frame is in a slot of its ring (FrameInSlot).
    void frame_in_slot(const rwin::FrameInSlot& frame);
    // A window's frames stopped, the window still there; they come again
    // by themselves.
    void surface_lost(rwin::WindowId window);
    void cursor_pictured(const rwin::CursorPicture& picture);
    void cursor_changed(const rwin::GuestCursor& cursor);
    void pointer_grabbed(const rwin::PointerGrab& grab);
    // The guest answered a clock probe; ``heard`` is this side's clock
    // (now_ns) as the answer was read.
    void clock_echoed(const rwin::ClockEcho& echo, qint64 heard);

private:
    void accept_guest();
    void read_available();
    // The guest's first message, which has to be its hello.
    void greet(const std::uint8_t* payload, std::size_t size);
    void refuse(const QString& reason);
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
    // Sent when ``feature`` is active on the link; otherwise said in the
    // log and dropped.
    void send(std::string_view feature, std::vector<std::uint8_t> bytes);
    void send_window(std::string_view feature, MessageId id, WindowId window);

    QTcpServer* server_;
    QTcpSocket* guest_ = nullptr;
    FrameDecoder decoder_;
    FeatureSet features_;
    GuestWindows windows_;
    bool greeted_ = false;
};

}  // namespace rwin::host
