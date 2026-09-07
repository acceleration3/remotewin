#pragma once

#include <cstdint>
#include <vector>

namespace rwin::guest {

class WindowSurface;

// The visible pixels of one window, into buffers that outlive the frame.
//
// PrintWindow draws the whole window rect, invisible resize border and
// all; what gets sent is the visible frame cropped out of it, so the
// image the host shows and the coordinates it sends back are measured
// from the same corner.
//
// Both buffers are kept between frames and only rebuilt when the window
// changes size: a capture that allocated per frame would spend more time
// in the allocator than in the copy.
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
    [[nodiscard]] bool grab(const WindowSurface& surface);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::uint8_t* pixels() const noexcept { return visible_.data(); }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return visible_.size(); }

private:
    [[nodiscard]] bool resize(const WindowSurface& surface);
    void crop(const WindowSurface& surface);
    void release();

    void* device_context_ = nullptr;
    void* bitmap_ = nullptr;
    void* previous_bitmap_ = nullptr;
    std::uint8_t* captured_ = nullptr;
    std::vector<std::uint8_t> visible_;
    std::uint32_t captured_width_ = 0;
    std::uint32_t captured_height_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

}  // namespace rwin::guest
