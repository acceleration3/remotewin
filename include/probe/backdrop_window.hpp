#pragma once

#include <cstdint>

struct HWND__;

namespace rwin::probe {

// A window of one flat colour over the whole screen, put behind the probe's
// windows so that what shows through them is a colour the harness knows
// and can put behind the mirror too. Transparency is only measurable
// against a known background.
//
// It lives in a process of its own. The mirror shows every window of the
// probe's process, and a screen-sized window in it would be mirrored over
// the host's whole desktop.
class BackdropWindow {
public:
    // ``rgb`` is 0x00RRGGBB.
    explicit BackdropWindow(std::uint32_t rgb);
    ~BackdropWindow();

    BackdropWindow(const BackdropWindow&) = delete;
    BackdropWindow& operator=(const BackdropWindow&) = delete;
    BackdropWindow(BackdropWindow&&) = delete;
    BackdropWindow& operator=(BackdropWindow&&) = delete;

    [[nodiscard]] bool made() const noexcept { return window_ != nullptr; }

    // Pumps messages until the process ``parent_process_id`` ends: the
    // backdrop outlives nothing it was put up for.
    void stay_while(std::uint32_t parent_process_id) const;

    static constexpr const wchar_t* kClassName = L"rwin-probe-backdrop";

private:
    HWND__* window_ = nullptr;
    void* brush_ = nullptr;
};

}  // namespace rwin::probe
