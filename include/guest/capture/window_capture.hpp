#pragma once

#include "guest/capture/capture_result.hpp"
#include "guest/capture/capture_stream.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::guest {

class FrameSignal;
class GraphicsDevice;
class WindowSurface;

// The visible pixels of one window, with its transparency intact.
//
// A frame holds the window's extended frame bounds - not its rectangle,
// whose invisible resize border the compositor leaves out - in the top
// left of the buffer, the frame's content size large. That is the picture
// WindowSurface measures every coordinate from.
class WindowCapture {
public:
    WindowCapture(const GraphicsDevice& device, const FrameSignal& arrivals) : stream_(device, arrivals) {}

    [[nodiscard]] CaptureResult grab(const WindowSurface& surface);
    // The next frame left on the GPU, in ``frame``: Fresh with it set,
    // Unchanged, or Gone. Nothing below describes it until it is kept.
    [[nodiscard]] CaptureResult grab(const WindowSurface& surface, std::optional<CaptureStream::Frame>& frame);
    // The window was seen minimised: frames until one is its size again are
    // what Windows captured of it as it went.
    void minimized() noexcept;
    // ``frame`` read back and trimmed, as grab keeps every frame: for one
    // taken to stay on the GPU that could not.
    // False when it could not be read back.
    [[nodiscard]] bool keep(const CaptureStream::Frame& frame, const WindowSurface& surface);

    // The drawn part only: a window's reserved shadow margin is trimmed
    // away, so what is sent is what the window actually put on screen. A
    // window with no margin to trim is sent whole.
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    // Where that part sits inside the picture WindowSurface describes, so
    // the host can place the window by what is visible rather than by what
    // is reserved.
    [[nodiscard]] std::uint32_t offset_x() const noexcept { return offset_x_; }
    [[nodiscard]] std::uint32_t offset_y() const noexcept { return offset_y_; }
    [[nodiscard]] const std::uint8_t* pixels() const noexcept { return visible_.data(); }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return visible_.size(); }

private:
    [[nodiscard]] bool follow(const WindowSurface& surface);
    void keep_drawn(CaptureStream::Extent content, const WindowSurface& surface);
    void keep_whole(CaptureStream::Extent content);

    static constexpr int kMostStaleFrames = 10;

    CaptureStream stream_;
    int stale_frames_ = 0;
    // The frame as it arrived, before trimming; kept so neither
    // buffer is reallocated per frame.
    std::vector<std::uint8_t> scratch_;
    std::vector<std::uint8_t> visible_;
    std::uint64_t started_for_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t offset_x_ = 0;
    std::uint32_t offset_y_ = 0;
};

}  // namespace rwin::guest
