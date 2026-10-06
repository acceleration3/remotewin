#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The pointer at a point of the guest's screen, rather than of a window:
// while a button pressed in a mirror is held. Windows may be moving that
// very window under the pointer - a tool panel being dragged - and the
// mirror is a step behind it, so a point taken against the mirror and
// sent against the window lands a step ahead and drags the window
// further. The host works the screen point out from where the press was
// and the pointer's own motion since, which no window's place enters.
// ``button`` is kNoButton for a move, else the PointerInjector button
// coming up or going down there.
struct PointerAt {
    static constexpr std::uint8_t kNoButton = 0xFF;

    // The guest window the button was pressed in.
    std::uint64_t window = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint8_t button = kNoButton;
    bool pressed = false;

    [[nodiscard]] bool operator==(const PointerAt&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerAt> read(MessageReader& in);
};

}  // namespace rwin
