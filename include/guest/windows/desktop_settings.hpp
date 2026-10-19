#pragma once

#include "proto/rect.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace rwin::guest {

class FrameSignal;

// The desktop-wide settings a window's report depends on, kept current:
// whether Windows shades windows at all (SPI_GETDROPSHADOW, "Show shadows
// under windows") and the primary monitor's work area.
//
// Read once at the start and again whenever Windows broadcasts
// WM_SETTINGCHANGE or WM_DISPLAYCHANGE, which go to top-level windows only:
// so this keeps a hidden one, on a thread of its own with the message loop
// it needs, and raises ``changes`` when either moved.
class DesktopSettings {
public:
    explicit DesktopSettings(const FrameSignal& changes);
    ~DesktopSettings();

    DesktopSettings(const DesktopSettings&) = delete;
    DesktopSettings& operator=(const DesktopSettings&) = delete;
    DesktopSettings(DesktopSettings&&) = delete;
    DesktopSettings& operator=(DesktopSettings&&) = delete;

    [[nodiscard]] bool drop_shadows() const noexcept { return drop_shadows_.load(); }
    [[nodiscard]] Rect work_area() const;
    // The primary monitor, whole.
    [[nodiscard]] Rect monitor() const;
    // Whether anything changed since the last call.
    [[nodiscard]] bool changed() noexcept { return changed_.exchange(false); }

    // Called from the hidden window's procedure, on this object's thread.
    void reread();

private:
    void listen();

    const FrameSignal& changes_;
    std::atomic<bool> drop_shadows_{false};
    mutable std::mutex lock_;
    Rect work_area_;
    Rect monitor_;
    std::atomic<bool> changed_{false};
    std::atomic<unsigned long> thread_id_{0};
    std::thread worker_;
};

}  // namespace rwin::guest
