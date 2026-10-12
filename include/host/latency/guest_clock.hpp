#pragma once

#include "host/latency/clock_offset.hpp"

#include <QObject>

#include <cstdint>
#include <optional>

class QTimer;

namespace rwin {
struct ClockEcho;
}

namespace rwin::host {

class GuestLink;

// The guest's clock as this side sees it: the guest is probed while it is
// connected (ClockProbe), quickly at first and then once a second, and its
// answers kept (ClockOffset).
class GuestClock : public QObject {
    Q_OBJECT
public:
    explicit GuestClock(GuestLink& link, QObject* parent = nullptr);

    // A moment the guest stamped, on this side's clock (now_ns); nothing
    // until the guest has answered a probe.
    [[nodiscard]] std::optional<std::int64_t> to_host(std::int64_t guest) const noexcept {
        return offset_.to_host(guest);
    }

private:
    void probe();
    void heard(const ClockEcho& echo, qint64 when);

    GuestLink& link_;
    QTimer* timer_;
    ClockOffset offset_;
    std::size_t answers_ = 0;
};

}  // namespace rwin::host
