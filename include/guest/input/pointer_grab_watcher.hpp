#pragma once

#include "proto/pointer_grab.hpp"
#include "proto/pointer_grab_rule.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin::guest {
class PointerGrabWatcher {
public:
    [[nodiscard]] std::optional<PointerGrab> changed(WindowId foreground);

    void put_at(std::int32_t screen_x, std::int32_t screen_y);
    void nudging();

    [[nodiscard]] const PointerGrab& current() const noexcept { return last_; }

private:
    PointerGrabRule rule_;
    PointerGrab last_;
};

}
