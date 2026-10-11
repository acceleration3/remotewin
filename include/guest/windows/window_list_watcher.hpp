#pragma once

#include "guest/windows/application_process.hpp"
#include "guest/windows/foreground_hand_off.hpp"
#include "guest/windows/launch_scope.hpp"
#include "guest/windows/window_enumerator.hpp"

#include <cstdint>
#include <optional>
#include <string>
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
    WindowListWatcher(const std::string& job_name, std::uint32_t process_id)
        : scope_(job_name, process_id), hand_off_(applications_), windows_(applications_) {}

    // The new set, or nothing when it is the set reported last. Geometry
    // and titles count as part of the set: a window that moved has to
    // reach the host as promptly as one that opened, or a menu appears
    // where the last one was.
    [[nodiscard]] std::optional<std::vector<GuestWindow>> changed();

    // Whether the application still has a process, as of the last look.
    // Part of the set: a list that is empty because the program is
    // between its stub and itself is a different list from one that is
    // empty because it exited.
    [[nodiscard]] bool application_alive() const noexcept { return alive_; }

    // Forces the next look to report, whatever it finds. Used when the
    // host asks outright.
    void forget() noexcept { known_.clear(); }

    // Input this mirror put into the guest: what the application does
    // next with the foreground is its doing (ForegroundHandOff).
    void note_input() noexcept { hand_off_.note_input(); }

private:
    LaunchScope scope_;
    // Before the two that ask it.
    ApplicationProcess applications_;
    ForegroundHandOff hand_off_;
    WindowEnumerator windows_;
    std::vector<GuestWindow> known_;
    bool alive_ = true;
};

}  // namespace rwin::guest
