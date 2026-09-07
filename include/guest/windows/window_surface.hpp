#pragma once

#include <cstdint>

namespace rwin::guest {

// A guest window: where it is, how big it is, and whether it is showing.
//
// One rectangle, GetWindowRect, used for everything. It includes the
// invisible resize border a composited window carries, and that is fine:
// those pixels are transparent, so they arrive transparent and paint as
// nothing. What used to be here — asking DWM for the visible frame and
// cropping the capture to it — existed because the old capture went
// through GDI and lost the alpha channel, which turned that border into a
// visible band. Nothing needs correcting once the transparency survives.
class WindowSurface {
public:
    explicit WindowSurface(std::uint64_t window_handle);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    // Nothing to capture, and nowhere meaningful to put a pointer: a
    // minimised window is parked far off-screen.
    [[nodiscard]] bool minimized() const noexcept { return minimized_; }

    [[nodiscard]] std::int32_t screen_x(std::int32_t window_x) const noexcept {
        return left_ + window_x;
    }
    [[nodiscard]] std::int32_t screen_y(std::int32_t window_y) const noexcept {
        return top_ + window_y;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    [[nodiscard]] void* handle() const noexcept { return handle_; }

private:
    void* handle_;
    std::int32_t left_ = 0;
    std::int32_t top_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool minimized_ = false;
    bool valid_ = false;
};

}  // namespace rwin::guest
