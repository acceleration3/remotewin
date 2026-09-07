#pragma once

#include "guest/windows/window_enumerator.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::guest {

// Notices that the set of windows became a different set.
//
// Menus, dropdowns and tooltips exist for as long as a click, so being
// asked twice a second means a menu can open and close without the host
// ever hearing about it. The guest is already awake at frame rate, and
// enumerating is cheap and local, so it looks every frame and speaks only
// when something is different.
class WindowListWatcher {
public:
    explicit WindowListWatcher(std::uint32_t process_id) noexcept
        : process_id_(process_id) {}

    // The new set, or nothing when it is the set reported last. Geometry
    // and titles count as part of the set: a window that moved has to
    // reach the host as promptly as one that opened, or a menu appears
    // where the last one was.
    [[nodiscard]] std::optional<std::vector<GuestWindow>> changed();

    // Forces the next look to report, whatever it finds. Used when the
    // host asks outright.
    void forget() noexcept { known_.clear(); }

private:
    WindowEnumerator windows_;
    std::vector<GuestWindow> known_;
    std::uint32_t process_id_;
};

}  // namespace rwin::guest
