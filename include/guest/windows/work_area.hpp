#pragma once

#include <cstdint>

namespace rwin::guest {

// The part of the guest's screen a window can occupy: the desktop less
// the taskbar and anything else reserved (SPI_GETWORKAREA).
//
// It is the ceiling on a guest window's size, and the host is told it so
// that a mirror cannot be dragged past what the guest can answer with.
// Asked each time rather than remembered: a taskbar that hides, a screen
// that changes resolution, and the answer is different.
class WorkArea {
public:
    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;
    [[nodiscard]] std::int32_t left() const;
    [[nodiscard]] std::int32_t top() const;
};

}  // namespace rwin::guest
