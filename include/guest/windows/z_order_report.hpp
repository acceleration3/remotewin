#pragma once

#include "proto/z_order.hpp"

#include <chrono>
#include <optional>

namespace rwin::guest {

class MarkerWindow;
class Outbox;
class WindowTracker;

// Tells the host the guest's stacking and active window (ZOrder), once a
// change has settled (rdpshell's HookZOrderSync, docs/rail-model.md §2.6).
//
// Activation is asynchronous, so a report waits until the window that
// should be active - the one the host asked for, or the one Windows made
// active - is on top, or first below the topmost windows. While it is
// not, the report is looked at again after 10, 20, 40, 80, 160 and 320 ms,
// rdpshell's backoff, and then sent as it stands: a transient foreground
// is never reported, and a stuck one is not waited on for ever. A report
// that says what the last one said is not sent.
class ZOrderReport {
public:
    using Clock = std::chrono::steady_clock;

    ZOrderReport(const WindowTracker& tracker, const MarkerWindow& marker, Outbox& outbox) noexcept
        : tracker_(tracker), marker_(marker), outbox_(outbox) {}

    // The host asked for ``window`` - the marker's handle for none - to be
    // active: the next report waits for it rather than for whatever is.
    void expect(void* window) noexcept;

    // Something that may have changed the stacking or the active window.
    void look(Clock::time_point now);

    // When the next look is due, while a report is waiting.
    [[nodiscard]] std::optional<Clock::time_point> due() const noexcept { return due_; }

private:
    static constexpr int kFirstWaitMs = 10;
    static constexpr int kLastWaitMs = 320;

    struct Stacking {
        ZOrder order;
        // The first entry, and the first that is not topmost; the marker as
        // its own handle.
        void* top = nullptr;
        void* top_ordinary = nullptr;
        void* active = nullptr;
    };

    [[nodiscard]] Stacking current() const;
    void send(const ZOrder& order);

    const WindowTracker& tracker_;
    const MarkerWindow& marker_;
    Outbox& outbox_;
    void* expected_ = nullptr;
    int wait_ms_ = 0;
    std::optional<Clock::time_point> due_;
    std::optional<ZOrder> sent_;
};

}  // namespace rwin::guest
