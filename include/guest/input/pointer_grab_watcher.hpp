#pragma once

#include "proto/pointer_grab.hpp"
#include "proto/pointer_grab_rule.hpp"

#include <cstdint>
#include <optional>

namespace rwin::guest {

// Notices that the guest application wants the pointer held differently:
// looks at the cursor, its clip and the foreground, and asks the rule
// (PointerGrabRule) what that amounts to.
//
// Each move the host's pointer makes is told to it, before and after:
// what the application did with the last one - put the cursor back, or
// left it - is the evidence for mouse look, and it is weighed just before
// the next move replaces it.
class PointerGrabWatcher {
public:
    // The grab now, when it is not the grab answered with last.
    // ``foreground`` is the foreground window when it is one of the
    // mirrored application's, else 0.
    [[nodiscard]] std::optional<PointerGrab> changed(std::uint64_t foreground);

    // The host's pointer put the cursor at this screen point.
    void put_at(std::int32_t screen_x, std::int32_t screen_y);
    // The host's pointer is about to move the cursor by an amount.
    void nudging();

    // The grab as last answered: a locked pointer's buttons act where the
    // cursor is held, not where the host saw them.
    [[nodiscard]] const PointerGrab& current() const noexcept { return last_; }

private:
    PointerGrabRule rule_;
    PointerGrab last_;
};

}  // namespace rwin::guest
