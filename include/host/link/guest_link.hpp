#pragma once

#include "host/link/guest_window_info.hpp"
#include "proto/frame_decoder.hpp"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QObject>

#include <cstdint>

class QTcpServer;
class QTcpSocket;

namespace rwin::host {

// The connection to the guest, and the only thing that speaks the wire.
//
// The host listens and the guest dials in: reaching a guest port needs a
// forwarding rule declared before the VM boots, while the guest can
// always reach the host.
class GuestLink : public QObject {
    Q_OBJECT
public:
    explicit GuestLink(QObject* parent = nullptr);

    [[nodiscard]] bool listen(std::uint16_t port);

    void request_window_list(std::uint32_t process_id);
    void subscribe(std::uint64_t window);
    void unsubscribe(std::uint64_t window);
    void send_pointer_move(std::uint64_t window, std::int32_t x, std::int32_t y);
    void send_pointer_button(std::uint64_t window, std::int32_t x, std::int32_t y,
                             std::uint8_t button, bool pressed);
    void send_key(std::uint64_t window, std::uint32_t virtual_key,
                  std::uint32_t character, bool pressed);
    void send_close(std::uint64_t window);
    void send_focus(std::uint64_t window);
    void send_resize(std::uint64_t window, std::uint32_t width, std::uint32_t height);

Q_SIGNALS:
    void connected();
    void disconnected();
    void windows_listed(const QList<rwin::host::GuestWindowInfo>& windows);
    void frame_arrived(quint64 window, const QImage& pixels);
    void window_gone(quint64 window);
    void drag_started(quint64 window);
    void cursor_shape(quint64 id, quint32 width, quint32 height, quint32 hotspot_x,
                      quint32 hotspot_y, const QByteArray& pixels);
    void cursor_changed(quint64 id);

private:
    void accept_guest();
    void read_available();
    void dispatch(const std::uint8_t* payload, std::size_t size);
    void send(std::vector<std::uint8_t> bytes);

    QTcpServer* server_;
    QTcpSocket* guest_ = nullptr;
    FrameDecoder decoder_;
};

}  // namespace rwin::host
