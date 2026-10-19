#include "host/latency/guest_clock.hpp"

#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"
#include "proto/clock_echo.hpp"

namespace rwin::host {

namespace {

// Ten probes a tenth of a second apart, so frames are put on this side's
// clock from their first second; then one a second, to follow drift.
constexpr std::size_t kQuickProbes = 10;
constexpr std::chrono::milliseconds kQuick{100};
constexpr std::chrono::milliseconds kSteady{1000};
constexpr double kMicrosecondsPerNanosecond = 1e-3;

}  // namespace

GuestClock::GuestClock(EventLoop& loop, GuestLink& link)
    : link_(link), timer_(loop.timer([this] { link_.send_clock_probe(); })) {
    link_.connected.connect([this] {
        offset_ = ClockOffset{};
        answers_ = 0;
        timer_->start(kQuick, true);
        link_.send_clock_probe();
    });
    link_.disconnected.connect([this] { timer_->stop(); });
    link_.clock_echoed.connect([this](const ClockEcho& echo, std::int64_t when) { heard(echo, when); });
}

void GuestClock::heard(const ClockEcho& echo, std::int64_t when) {
    offset_.take(static_cast<std::int64_t>(echo.probed), echo.guest, when);
    if (++answers_ != kQuickProbes) return;
    timer_->start(kSteady, true);
    if (const std::optional<std::int64_t> round_trip = offset_.round_trip()) {
        RWIN_DEBUG("latency", "the guest's clock is known to within {:.0f} us",
                   static_cast<double>(*round_trip) * kMicrosecondsPerNanosecond / 2.0);
    }
}

}  // namespace rwin::host
