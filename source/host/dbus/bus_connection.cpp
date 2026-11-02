#include "host/dbus/bus_connection.hpp"

#include "logging/logger.hpp"

#include <systemd/sd-bus.h>

#include <poll.h>
#include <time.h>

#include <algorithm>
#include <cstring>

namespace rwin::host {

namespace {

constexpr std::uint64_t kMicrosecondsPerMillisecond = 1000;

std::uint64_t monotonic_us() {
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000 + static_cast<std::uint64_t>(now.tv_nsec) / 1000;
}

}

BusConnection::BusConnection(EventLoop& loop) {
    if (const int error = sd_bus_open_user(&bus_); error < 0) {
        RWIN_INFO("dbus", "no session bus: {}", std::strerror(-error));
        bus_ = nullptr;
        return;
    }
    watch_ = loop.watch(sd_bus_get_fd(bus_), POLLIN, [this](short) { pump(); });
    timeout_ = loop.timer([this] { pump(); });
    rearm();
}

BusConnection::~BusConnection() {
    watch_.reset();
    timeout_.reset();
    if (bus_ != nullptr) sd_bus_flush_close_unref(bus_);
}

std::string BusConnection::unique_name() const {
    const char* name = nullptr;
    if (bus_ == nullptr || sd_bus_get_unique_name(bus_, &name) < 0 || name == nullptr) return {};
    return name;
}

void BusConnection::pump() {
    if (bus_ == nullptr) return;
    if (processing_) return;
    processing_ = true;
    while (true) {
        const int done = sd_bus_process(bus_, nullptr);
        if (done < 0) {
            RWIN_WARN("dbus", "the session bus failed: {}", std::strerror(-done));
            break;
        }
        if (done == 0) break;
    }
    processing_ = false;
    rearm();
}

void BusConnection::rearm() {
    const int events = sd_bus_get_events(bus_);
    if (events >= 0) watch_->want(static_cast<short>(events));
    std::uint64_t until = 0;
    if (sd_bus_get_timeout(bus_, &until) < 0 || until == UINT64_MAX) {
        timeout_->stop();
        return;
    }
    const std::uint64_t now = monotonic_us();
    const std::uint64_t wait = until > now ? (until - now + kMicrosecondsPerMillisecond - 1) / kMicrosecondsPerMillisecond : 0;
    timeout_->start(std::chrono::milliseconds(wait), false);
}

}
