#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

// A rectangle of the guest's screen as the compositor composed it: every
// window in it, at the opacity and shape DWM gave each, over whatever was
// behind. The guest half of a pixel comparison.
struct ScreenPixels {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // BGRA, top-down, ``width * 4`` per row, alpha 255 throughout: the
    // screen has no transparency left in it.
    std::vector<std::uint8_t> bgra;

    void write(MessageWriter& out) const;
    // Nothing when the message is short or the pixels are not the size
    // the rectangle says.
    [[nodiscard]] static std::optional<ScreenPixels> read(MessageReader& in);
};

}  // namespace rwin
