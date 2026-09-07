#pragma once

#include <cstdint>
#include <vector>

namespace rwin::guest {

// Pixels of one window, into a buffer that outlives the frame.
//
// The device context and the bitmap are kept between frames and only
// rebuilt when the window changes size: a capture that allocated per
// frame would spend more time in the allocator than in the copy.
class WindowCapture {
public:
    WindowCapture();
    ~WindowCapture();

    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;
    WindowCapture(WindowCapture&&) = delete;
    WindowCapture& operator=(WindowCapture&&) = delete;

    // False when the window is gone or refused to draw. On success the
    // pixels are BGRA, top-down, tightly packed at width * 4.
    [[nodiscard]] bool grab(std::uint64_t window_handle);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::uint8_t* pixels() const noexcept { return pixels_; }
    [[nodiscard]] std::size_t size_bytes() const noexcept {
        return static_cast<std::size_t>(width_) * height_ * 4;
    }

private:
    [[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height);
    void release();

    void* device_context_ = nullptr;
    void* bitmap_ = nullptr;
    void* previous_bitmap_ = nullptr;
    std::uint8_t* pixels_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

}  // namespace rwin::guest
