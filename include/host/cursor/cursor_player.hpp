#pragma once

#include "host/cursor/cursor_animation.hpp"
#include "host/cursor/emulated_cursor.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "proto/guest_cursor.hpp"

#include <cstddef>
#include <cstdint>

namespace rwin {
struct CursorPicture;
}

namespace rwin::host {

struct CursorLook;

class CursorPlayer {
public:
    explicit CursorPlayer(EventLoop& loop);

    void remember(const CursorPicture& picture);
    void show(const GuestCursor& cursor);

    [[nodiscard]] const CursorLook* look() const;
    [[nodiscard]] const GuestCursor& shown() const noexcept { return shown_; }
    [[nodiscard]] std::size_t step() const noexcept { return animation_.step(); }
    [[nodiscard]] std::uint64_t steps_taken() const noexcept { return animation_.steps_taken(); }

    Signal<const CursorLook*, std::size_t> worn;

private:
    void step_to(std::size_t step);

    EmulatedCursor looks_;
    CursorAnimation animation_;
    GuestCursor shown_;
};

}
