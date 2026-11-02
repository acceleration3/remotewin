#include "guest/capture/window_capture.hpp"

#include "guest/capture/content_bounds.hpp"
#include "guest/capture/picture_room.hpp"
#include "guest/windows/window_surface.hpp"

#include <cstring>
#include <optional>

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;

}

CaptureResult WindowCapture::grab(const WindowSurface& surface) {
    std::optional<CaptureStream::Frame> frame;
    const CaptureResult found = grab(surface, frame);
    if (found != CaptureResult::Fresh) return found;
    return keep(*frame, surface) ? CaptureResult::Fresh : CaptureResult::Unchanged;
}

CaptureResult WindowCapture::grab(const WindowSurface& surface, std::optional<CaptureStream::Frame>& frame) {
    if (!follow(surface)) return CaptureResult::Gone;
    frame = stream_.next_frame();
    if (!frame.has_value()) {
        if (const std::optional<CaptureStream::Extent> outgrown = stream_.outgrown()) {
            if (!stream_.capture_afresh(room_for(*outgrown))) return CaptureResult::Gone;
        }
        return CaptureResult::Unchanged;
    }
    if (stale_frames_ > 0) {
        if (frame->content() != CaptureStream::Extent{surface.width(), surface.height()}) {
            --stale_frames_;
            frame.reset();
            return CaptureResult::Unchanged;
        }
        stale_frames_ = 0;
    }
    return CaptureResult::Fresh;
}

void WindowCapture::minimized() noexcept { stale_frames_ = kMostStaleFrames; }

bool WindowCapture::keep(const CaptureStream::Frame& frame, const WindowSurface& surface) {
    const std::optional<CaptureStream::Extent> content = stream_.read(frame, scratch_);
    if (!content.has_value()) return false;
    keep_drawn(*content, surface);
    return true;
}

bool WindowCapture::follow(const WindowSurface& surface) {
    if (!surface.valid()) return false;
    const auto handle =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface.handle()));
    const CaptureStream::Extent size{surface.width(), surface.height()};
    if (started_for_ != handle) {
        started_for_ = 0;
        if (!stream_.open_window(surface.handle(), room_for(size))) return false;
        started_for_ = handle;
    }
    if (room_holds(stream_.size(), size)) return true;
    const bool fits = size.width <= stream_.size().width && size.height <= stream_.size().height;
    return fits ? stream_.resize(room_for(size)) : stream_.capture_afresh(room_for(size));
}

void WindowCapture::keep_drawn(CaptureStream::Extent content, const WindowSurface& surface) {
    if (!surface.margin_is_reserved()) {
        keep_whole(content);
        return;
    }
    const ContentBounds drawn{scratch_.data(), content.width, content.height};
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

void WindowCapture::keep_whole(CaptureStream::Extent content) {
    offset_x_ = 0;
    offset_y_ = 0;
    width_ = content.width;
    height_ = content.height;
    visible_.swap(scratch_);
}

}
