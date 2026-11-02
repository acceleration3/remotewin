#pragma once

#include "guest/capture/capture_result.hpp"
#include "guest/capture/capture_stream.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::guest {

class CaptureCloser;
class FrameSignal;
class GraphicsDevice;

// The guest's primary monitor, whole, as the compositor composed it: the
// desktop, the taskbar and every window on it, without the cursor.
//
// Followed as it changes: a new resolution is frames of the new size, and
// a monitor Windows replaced (a display reconfigured) is captured afresh.
class MonitorCapture {
public:
    MonitorCapture(const GraphicsDevice& device, const FrameSignal& arrivals, CaptureCloser& closer)
        : stream_(device, arrivals, closer) {}

    [[nodiscard]] CaptureResult grab();
    // The next frame left on the GPU, in ``frame``: Fresh with it set,
    // Unchanged, or Gone. width() and height() are its picture's.
    [[nodiscard]] CaptureResult grab(std::optional<CaptureStream::Frame>& frame);
    // ``frame`` read back as grab reads every frame: for one taken to stay
    // on the GPU that could not.
    // False when it could not be read back.
    [[nodiscard]] bool keep(const CaptureStream::Frame& frame);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::uint8_t* pixels() const noexcept { return pixels_.data(); }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return pixels_.size(); }

    // A point of the picture on the guest's screen.
    [[nodiscard]] std::int32_t screen_x(std::int32_t picture_x) const noexcept { return left_ + picture_x; }
    [[nodiscard]] std::int32_t screen_y(std::int32_t picture_y) const noexcept { return top_ + picture_y; }

private:
    [[nodiscard]] bool follow();

    CaptureStream stream_;
    std::vector<std::uint8_t> pixels_;
    // The HMONITOR captured.
    void* monitor_ = nullptr;
    std::int32_t left_ = 0;
    std::int32_t top_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

}  // namespace rwin::guest
