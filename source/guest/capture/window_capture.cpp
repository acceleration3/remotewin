#include "guest/capture/window_capture.hpp"

#include "guest/capture/content_bounds.hpp"
#include "guest/windows/window_surface.hpp"

#include <cstring>
#include <optional>

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

}  // namespace

CaptureResult WindowCapture::grab(const WindowSurface& surface) {
    std::optional<CaptureStream::Frame> frame;
    const CaptureResult found = grab(surface, frame);
    if (found != CaptureResult::Fresh) return found;
    return keep(*frame, surface) ? CaptureResult::Fresh : CaptureResult::Unchanged;
}

CaptureResult WindowCapture::grab(const WindowSurface& surface, std::optional<CaptureStream::Frame>& frame) {
    if (!follow(surface)) return CaptureResult::Gone;
    frame = stream_.next_frame();
    return frame.has_value() ? CaptureResult::Fresh : CaptureResult::Unchanged;
}

bool WindowCapture::keep(const CaptureStream::Frame& frame, const WindowSurface& surface) {
    const std::optional<CaptureStream::Extent> content = stream_.read(frame, scratch_);
    if (!content.has_value()) return false;
    keep_drawn(*content, surface);
    return true;
}

// Captured from the window's own first frame, and at its size: frames of
// another size would crop it or pad it.
bool WindowCapture::follow(const WindowSurface& surface) {
    if (!surface.valid()) return false;
    const auto handle =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface.handle()));
    const CaptureStream::Extent size{surface.width(), surface.height()};
    if (started_for_ != handle) {
        started_for_ = 0;
        if (!stream_.open_window(surface.handle(), size)) return false;
        started_for_ = handle;
    }
    return stream_.size() == size || stream_.resize(size);
}

// Not a margin (WindowSurface::margin_is_reserved): a shaped window's
// transparent edge is its region, which is in the window's coordinates -
// trimmed, an ellipse lost its last row and column and every point
// measured against it moved with them.
void WindowCapture::keep_drawn(CaptureStream::Extent content, const WindowSurface& surface) {
    if (!surface.margin_is_reserved()) {
        keep_whole(content);
        return;
    }
    const ContentBounds drawn{scratch_.data(), content.width, content.height};
    // A window that draws nothing at all - WS_EX_NOREDIRECTIONBITMAP with
    // nothing composed into it, or one entirely see-through - is still on
    // screen: sent whole and transparent, rather than never.
    if (drawn.empty()) {
        keep_whole(content);
        return;
    }

    offset_x_ = drawn.left();
    offset_y_ = drawn.top();
    width_ = drawn.width();
    height_ = drawn.height();

    const std::size_t source_row = static_cast<std::size_t>(content.width) * kBytesPerPixel;
    const std::size_t row = static_cast<std::size_t>(width_) * kBytesPerPixel;
    visible_.resize(row * height_);
    const std::size_t left = static_cast<std::size_t>(offset_x_) * kBytesPerPixel;
    for (std::uint32_t y = 0; y < height_; ++y) {
        std::memcpy(visible_.data() + y * row,
                    scratch_.data() + (offset_y_ + y) * source_row + left, row);
    }
}

// The frame as it arrived is what is sent. Swapped rather than copied:
// both buffers keep their size from frame to frame either way.
void WindowCapture::keep_whole(CaptureStream::Extent content) {
    offset_x_ = 0;
    offset_y_ = 0;
    width_ = content.width;
    height_ = content.height;
    visible_.swap(scratch_);
}

}  // namespace rwin::guest
