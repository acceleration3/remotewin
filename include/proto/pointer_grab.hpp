#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// What the guest application wants done with the pointer, as the guest
// half judged it (PointerGrabRule): left alone, kept inside a rectangle of
// the screen (ClipCursor), or held still while only its motion is wanted -
// the first-person game's mouse look.
struct PointerGrab {
    enum class Kind : std::uint8_t { None, Confine, Lock };

    // Guest screen pixels, right and bottom exclusive.
    struct Area {
        std::int32_t left = 0;
        std::int32_t top = 0;
        std::int32_t right = 0;
        std::int32_t bottom = 0;

        [[nodiscard]] std::int32_t width() const noexcept { return right - left; }
        [[nodiscard]] std::int32_t height() const noexcept { return bottom - top; }
        [[nodiscard]] bool operator==(const Area&) const noexcept = default;
    };

    Kind kind = Kind::None;
    // The guest window it is for: the foreground one.
    std::uint64_t window = 0;
    // Confine: the clip rectangle. Lock: the clip rectangle when there is
    // one, else empty.
    Area area;
    // Lock: where the guest holds its cursor, in guest screen pixels - the
    // point it keeps returning the cursor to, or the clip's middle.
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] bool operator==(const PointerGrab&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerGrab> read(MessageReader& in);
};

}  // namespace rwin
