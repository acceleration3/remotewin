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

class MonitorCapture {
public:
    MonitorCapture(const GraphicsDevice& device, const FrameSignal& arrivals, CaptureCloser& closer)
        : stream_(device, arrivals, closer) {}

    [[nodiscard]] CaptureResult grab();
    [[nodiscard]] CaptureResult grab(std::optional<CaptureStream::Frame>& frame);
    [[nodiscard]] bool keep(const CaptureStream::Frame& frame);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::uint8_t* pixels() const noexcept { return pixels_.data(); }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return pixels_.size(); }

    [[nodiscard]] std::int32_t screen_x(std::int32_t picture_x) const noexcept { return left_ + picture_x; }
    [[nodiscard]] std::int32_t screen_y(std::int32_t picture_y) const noexcept { return top_ + picture_y; }

private:
    [[nodiscard]] bool follow();

    CaptureStream stream_;
    std::vector<std::uint8_t> pixels_;
    void* monitor_ = nullptr;
    std::int32_t left_ = 0;
    std::int32_t top_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

}
