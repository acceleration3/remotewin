#pragma once

#include "proto/rect.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace rwin::guest {

class FrameSignal;

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
    [[nodiscard]] Rect monitor() const;
    [[nodiscard]] bool changed() noexcept { return changed_.exchange(false); }

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

}
