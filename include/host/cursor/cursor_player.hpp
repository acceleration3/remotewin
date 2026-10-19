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

// The guest's cursor as it goes: every picture it has sent, the one it
// shows now, and - for an animated one this desktop does not draw by name,
// which animates its own - the step it is at, shown for its own time.
//
// The guest has one cursor at a time and does not say whose window it is
// over, so every mirror wears what this says (``worn``); only the one under
// the pointer is showing a cursor at all.
class CursorPlayer {
public:
    explicit CursorPlayer(EventLoop& loop);

    void remember(const CursorPicture& picture);
    void show(const GuestCursor& cursor);

    // What the guest shows now; null for no cursor at all.
    [[nodiscard]] const CursorLook* look() const;
    [[nodiscard]] const GuestCursor& shown() const noexcept { return shown_; }
    [[nodiscard]] std::size_t step() const noexcept { return animation_.step(); }
    [[nodiscard]] std::uint64_t steps_taken() const noexcept { return animation_.steps_taken(); }

    // Wear the look at the step; null is no cursor at all.
    Signal<const CursorLook*, std::size_t> worn;

private:
    void step_to(std::size_t step);

    EmulatedCursor looks_;
    CursorAnimation animation_;
    GuestCursor shown_;
};

}  // namespace rwin::host
