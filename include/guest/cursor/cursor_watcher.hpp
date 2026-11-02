#pragma once

#include "proto/guest_cursor.hpp"

#include <atomic>
#include <optional>
#include <thread>

namespace rwin::guest {

class FrameSignal;

class CursorWatcher {
public:
    explicit CursorWatcher(const FrameSignal& changes);
    ~CursorWatcher();

    CursorWatcher(const CursorWatcher&) = delete;
    CursorWatcher& operator=(const CursorWatcher&) = delete;
    CursorWatcher(CursorWatcher&&) = delete;
    CursorWatcher& operator=(CursorWatcher&&) = delete;

    [[nodiscard]] std::optional<GuestCursor> changed();

    void heard() const noexcept;

private:
    void listen();

    const FrameSignal& changes_;
    std::optional<GuestCursor> last_;
    std::thread worker_;
    std::atomic<unsigned long> thread_id_{0};
};

}
