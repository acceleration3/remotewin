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
class WindowSurface;

class WindowCapture {
public:
    WindowCapture(const GraphicsDevice& device, const FrameSignal& arrivals, CaptureCloser& closer)
        : stream_(device, arrivals, closer) {}

    [[nodiscard]] CaptureResult grab(const WindowSurface& surface);
    [[nodiscard]] CaptureResult grab(const WindowSurface& surface, std::optional<CaptureStream::Frame>& frame);
    void minimized() noexcept;
    [[nodiscard]] bool keep(const CaptureStream::Frame& frame, const WindowSurface& surface);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
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
    std::vector<std::uint8_t> scratch_;
    std::vector<std::uint8_t> visible_;
    std::uint64_t started_for_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t offset_x_ = 0;
    std::uint32_t offset_y_ = 0;
};

}
