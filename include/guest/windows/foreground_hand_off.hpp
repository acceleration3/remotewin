#pragma once

#include <cstdint>
#include <optional>

namespace rwin::guest {

class ApplicationProcess;
class LaunchScope;

// Notices the application handing the foreground to another program: a
// link opened in the browser that is already running, a document in the
// program that opens it. The program started for it often passes the
// request to a running instance and exits at once, too soon to be seen as
// the application's child, and the running instance owes the application
// nothing - but Windows gives it the foreground, since only the
// foreground's process may.
//
// So: the foreground moved from a window of the scope to one outside it,
// after input this mirror put into the guest and with no input since from
// anyone else, and with that window still on screen. Another mirror's
// click moves the foreground too, but its input is newer than this one's;
// a window closed or minimised passes the foreground on, but is gone.
class ForegroundHandOff {
public:
    explicit ForegroundHandOff(ApplicationProcess& applications) : applications_(applications) {}

    // Input this mirror put into the guest just now.
    void note_input() noexcept;

    // The process the application handed the foreground to since the last
    // look, if it did.
    [[nodiscard]] std::optional<std::uint32_t> handed_to(const LaunchScope& scope);

private:
    // How long after this mirror's input a hand-off is still its doing.
    static constexpr std::uint32_t kWithinMs = 5000;
    // How far the system's last input may trail this mirror's own, the
    // same input reaching the input queue a moment after it was sent.
    static constexpr std::uint32_t kSameInputMs = 100;

    ApplicationProcess& applications_;
    void* foreground_ = nullptr;
    bool foreground_in_scope_ = false;
    std::uint32_t last_input_ = 0;
};

}  // namespace rwin::guest
