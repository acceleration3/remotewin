#include "host/latency/guest_clock.hpp"

#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"
#include "proto/clock_echo.hpp"

#include <QTimer>

namespace rwin::host {

namespace {

// Ten probes a tenth of a second apart, so frames are put on this side's
// clock from their first second; then one a second, to follow drift.
constexpr std::size_t kQuickProbes = 10;
constexpr int kQuickMs = 100;
constexpr int kSteadyMs = 1000;
constexpr double kMicrosecondsPerNanosecond = 1e-3;

}  // namespace

GuestClock::GuestClock(GuestLink& link, QObject* parent)
    : QObject(parent), link_(link), timer_(new QTimer(this)) {
    timer_->setInterval(kQuickMs);
    connect(timer_, &QTimer::timeout, this, &GuestClock::probe);
    connect(&link_, &GuestLink::connected, this, [this] {
        offset_ = ClockOffset{};
        answers_ = 0;
        timer_->setInterval(kQuickMs);
        timer_->start();
        probe();
    });
    connect(&link_, &GuestLink::disconnected, timer_, &QTimer::stop);
    connect(&link_, &GuestLink::clock_echoed, this, &GuestClock::heard);
}

void GuestClock::probe() { link_.send_clock_probe(); }

void GuestClock::heard(const ClockEcho& echo, qint64 when) {
    offset_.take(static_cast<std::int64_t>(echo.probed), echo.guest, when);
    if (++answers_ != kQuickProbes) return;
    timer_->setInterval(kSteadyMs);
    if (const std::optional<std::int64_t> round_trip = offset_.round_trip()) {
        RWIN_DEBUG("latency", "the guest's clock is known to within {:.0f} us",
                   static_cast<double>(*round_trip) * kMicrosecondsPerNanosecond / 2.0);
    }
}

}  // namespace rwin::host
