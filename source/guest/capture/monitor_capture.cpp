#include "guest/capture/monitor_capture.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <optional>

namespace rwin::guest {

CaptureResult MonitorCapture::grab() {
    std::optional<CaptureStream::Frame> frame;
    const CaptureResult found = grab(frame);
    if (found != CaptureResult::Fresh) return found;
    return keep(*frame) ? CaptureResult::Fresh : CaptureResult::Unchanged;
}

CaptureResult MonitorCapture::grab(std::optional<CaptureStream::Frame>& frame) {
    if (!follow()) return CaptureResult::Gone;
    frame = stream_.next_frame();
    if (!frame.has_value()) return CaptureResult::Unchanged;
    width_ = frame->content().width;
    height_ = frame->content().height;
    return CaptureResult::Fresh;
}

bool MonitorCapture::keep(const CaptureStream::Frame& frame) {
    const std::optional<CaptureStream::Extent> content = stream_.read(frame, pixels_);
    if (!content.has_value()) return false;
    width_ = content->width;
    height_ = content->height;
    return true;
}

// Physical pixels throughout: the process is per-monitor DPI aware, so the
// monitor's rectangle is in the same pixels as the frames and as the
// screen the pointer is put on.
bool MonitorCapture::follow() {
    HMONITOR monitor = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO described{};
    described.cbSize = sizeof(described);
    if (monitor == nullptr || ::GetMonitorInfoW(monitor, &described) == FALSE) {
        RWIN_WARN("capture", "no primary monitor to capture");
        return false;
    }
    const RECT& bounds = described.rcMonitor;
    const CaptureStream::Extent size{static_cast<std::uint32_t>(bounds.right - bounds.left),
                                     static_cast<std::uint32_t>(bounds.bottom - bounds.top)};
    if (monitor != monitor_) {
        monitor_ = nullptr;
        if (!stream_.open_monitor(monitor, size)) return false;
        monitor_ = monitor;
        RWIN_INFO("capture", "capturing the primary monitor, {}x{} at {},{}", size.width,
                  size.height, bounds.left, bounds.top);
    }
    if (stream_.size() != size) {
        RWIN_INFO("capture", "the monitor is {}x{} now", size.width, size.height);
        if (!stream_.resize(size)) return false;
    }
    left_ = bounds.left;
    top_ = bounds.top;
    return true;
}

}  // namespace rwin::guest
