#pragma once

#include "host/link/guest_window_info.hpp"
#include "proto/clock_echo.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/slot_ring.hpp"

#include <QHostAddress>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <cstdint>

// Signal arguments are complete types: Qt's meta-type registration of a
// signal's arguments looks inside them.
namespace rwin {
struct PointerAt;
struct PointerWheel;
struct KeyStroke;
class MessageReader;
struct SlotFree;
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
// Each end says first which wire it speaks (WireHello). Nothing the guest
// sends is read until its hello has said the same version as this one: a
// guest half from another build is refused with both versions named,
// rather than its messages misread field by field.
class GuestLink : public QObject {
    Q_OBJECT
public:
    explicit GuestLink(QObject* parent = nullptr);

    [[nodiscard]] bool listen(std::uint16_t port,
                              const QHostAddress& address = QHostAddress::AnyIPv4);
    [[nodiscard]] std::uint16_t port() const;

    void watch_scope(std::uint32_t process_id, const QString& job_name);
    // The guest's whole primary monitor, instead of an application.
    void watch_monitor();
    void subscribe(std::uint64_t window);
    void unsubscribe(std::uint64_t window);
    void send_pointer_move(std::uint64_t window, std::int32_t x, std::int32_t y);
    void send_pointer_button(std::uint64_t window, std::int32_t x, std::int32_t y,
                             std::uint8_t button, bool pressed);
    // The held pointer's own motion, in counts.
    void send_pointer_delta(std::uint64_t window, std::int32_t dx, std::int32_t dy);
    void send_pointer_wheel(const PointerWheel& wheel);
    void send_pointer_at(const PointerAt& at);
    void send_key(const KeyStroke& stroke);
    void send_close(std::uint64_t window);
    void send_focus(std::uint64_t window);
    void send_resize(std::uint64_t window, std::uint32_t width, std::uint32_t height);
    // The window's picture corner, in guest screen pixels.
    void send_move(std::uint64_t window, std::int32_t x, std::int32_t y);
    // Whether the guest is to keep its frames on the GPU (FrameRoute).
    void send_route(bool shared_surfaces);
    void send_slot_free(const SlotFree& freed);
    // This side's clock now, for the guest to answer (ClockEcho).
    void send_clock_probe();

Q_SIGNALS:
    // A guest dialled in and speaks this wire.
    void connected();
    // A guest dialled in that speaks another wire, or none: ``reason``
    // names both sides. It has been disconnected.
    void mismatched(const QString& reason);
    void disconnected();
    // ``application_alive`` is whether the guest application still has a
    // process. An empty list from a live application is a program between
    // windows, not one that is done.
    void windows_listed(const QList<rwin::host::GuestWindowInfo>& windows, bool application_alive,
                        QSize guest_room);
    void frame_arrived(quint64 window, const QImage& pixels);
    // A part of ``window``'s picture, which is ``picture`` large, changed:
    // ``pixels`` now go at ``at``.
    void frame_patched(quint64 window, QSize picture, QPoint at, const QImage& pixels);
    // A window's frames are copied into this ring of shared surfaces from
    // here on (SlotRing).
    void ring_announced(const rwin::SlotRing& ring);
    // A window's newest frame is in a slot of its ring (FrameInSlot).
    void frame_in_slot(const rwin::FrameInSlot& frame);
    void window_gone(quint64 window);
    void drag_started(quint64 window);
    void cursor_pictured(const rwin::CursorPicture& picture);
    void cursor_changed(const rwin::GuestCursor& cursor);
    void pointer_grabbed(const rwin::PointerGrab& grab);
    // The guest answered a clock probe; ``heard`` is this side's clock
    // (now_ns) as the answer was read.
    void clock_echoed(const rwin::ClockEcho& echo, qint64 heard);

private:
    void accept_guest();
    void read_available();
    void dispatch(const std::uint8_t* payload, std::size_t size);
    // The guest's first message, which has to be its hello.
    void greet(const std::uint8_t* payload, std::size_t size);
    void read_patch(MessageReader& message);
    void send(std::vector<std::uint8_t> bytes);

    QTcpServer* server_;
    QTcpSocket* guest_ = nullptr;
    FrameDecoder decoder_;
    bool greeted_ = false;
};

}  // namespace rwin::host
