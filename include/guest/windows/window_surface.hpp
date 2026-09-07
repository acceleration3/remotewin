#pragma once

#include <cstdint>

namespace rwin::guest {

// A guest window's visible frame: where it is on screen, and where it
// sits inside the bitmap a capture of it produces.
//
// GetWindowRect is the wrong rectangle for both. On a composited window
// it includes an invisible resize border several pixels wide on three
// sides, which is not drawn and which the user cannot see. Measuring
// input against it puts every click that far from where it looked, and
// showing it puts a band of nothing around the window. The visible frame
// is what DWM calls the extended frame bounds.
//
// PrintWindow still draws into a bitmap the size of GetWindowRect, so the
// capture crops: ``inset_x``/``inset_y`` are where the visible frame
// starts inside it.
class WindowSurface {
public:
    explicit WindowSurface(std::uint64_t window_handle);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    // Nothing to capture, and nowhere meaningful to put a pointer: a
    // minimised window is parked far off-screen.
    [[nodiscard]] bool minimized() const noexcept { return minimized_; }

    [[nodiscard]] std::int32_t screen_x(std::int32_t frame_x) const noexcept {
        return frame_left_ + frame_x;
    }
    [[nodiscard]] std::int32_t screen_y(std::int32_t frame_y) const noexcept {
        return frame_top_ + frame_y;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    [[nodiscard]] std::uint32_t captured_width() const noexcept { return captured_width_; }
    [[nodiscard]] std::uint32_t captured_height() const noexcept {
        return captured_height_;
    }
    [[nodiscard]] std::uint32_t inset_x() const noexcept { return inset_x_; }
    [[nodiscard]] std::uint32_t inset_y() const noexcept { return inset_y_; }

    [[nodiscard]] void* handle() const noexcept { return handle_; }

private:
    void* handle_;
    std::int32_t frame_left_ = 0;
    std::int32_t frame_top_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t captured_width_ = 0;
    std::uint32_t captured_height_ = 0;
    std::uint32_t inset_x_ = 0;
    std::uint32_t inset_y_ = 0;
    bool minimized_ = false;
    bool valid_ = false;
};

}  // namespace rwin::guest
