#pragma once

#include <atomic>
#include <thread>

namespace rwin::guest {
class MarkerWindow {
public:
    MarkerWindow();
    ~MarkerWindow();

    MarkerWindow(const MarkerWindow&) = delete;
    MarkerWindow& operator=(const MarkerWindow&) = delete;
    MarkerWindow(MarkerWindow&&) = delete;
    MarkerWindow& operator=(MarkerWindow&&) = delete;

    [[nodiscard]] void* handle() const noexcept { return marker_.load(); }

    void activate(void* window);

    void carry_out();
    void hotkey_heard() noexcept { hotkey_heard_ = true; }
    void timed_out() noexcept { timed_out_ = true; }

private:
    void run();
    void register_hotkey();
    [[nodiscard]] bool take_with_hotkey(void* window);

    std::thread worker_;
    std::atomic<void*> marker_{nullptr};
    std::atomic<void*> keys_{nullptr};
    std::atomic<void*> wanted_{nullptr};
    std::atomic<unsigned long> thread_id_{0};
    std::atomic<bool> ready_{false};
    int hotkey_id_ = 0;
    unsigned int hotkey_key_ = 0;
    bool carrying_out_ = false;
    bool hotkey_heard_ = false;
    bool timed_out_ = false;
};

}
