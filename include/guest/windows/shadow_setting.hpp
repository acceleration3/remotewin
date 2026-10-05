#pragma once

#include <atomic>
#include <thread>

namespace rwin::guest {

// Whether this desktop draws shadows under windows - SPI_GETDROPSHADOW,
// "Show shadows under windows" - kept current.
//
// Read once at the start and again whenever Windows broadcasts
// WM_SETTINGCHANGE, which goes to top-level windows only: so this keeps a
// hidden one, on a thread of its own with the message loop it needs. It
// is never shown, and being this process's, never in an application's
// window list.
class ShadowSetting {
public:
    ShadowSetting();
    ~ShadowSetting();

    ShadowSetting(const ShadowSetting&) = delete;
    ShadowSetting& operator=(const ShadowSetting&) = delete;
    ShadowSetting(ShadowSetting&&) = delete;
    ShadowSetting& operator=(ShadowSetting&&) = delete;

    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }
    // Whether it changed since the last call.
    [[nodiscard]] bool changed() noexcept { return changed_.exchange(false); }

    // Called from the hidden window's procedure, on this setting's thread.
    void reread();

private:
    void listen();

    std::atomic<bool> enabled_{false};
    std::atomic<bool> changed_{false};
    std::atomic<unsigned long> thread_id_{0};
    std::thread worker_;
};

}  // namespace rwin::guest
