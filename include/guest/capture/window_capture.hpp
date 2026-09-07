#pragma once

#include <cstdint>
#include <vector>

namespace rwin::guest {

class GraphicsDevice;
class WindowSurface;

// The visible pixels of one window, with its transparency intact.
//
// Taken from the compositor rather than asking the window to draw itself.
// PrintWindow goes through GDI, which has no alpha channel: every pixel
// comes back opaque, so a rounded corner is a black notch, a shadow is a
// black band, and a window that is meant to be see-through is not. What
// DWM already composed has all of that, and this asks for a copy of it.
//
// The compositor also draws things the window does not: a capture arrives
// the size of the window rect, invisible resize border included, and is
// cropped to the visible frame — the same rectangle the coordinates are
// measured against, so what is shown and what is clicked agree.
class WindowCapture {
public:
    explicit WindowCapture(const GraphicsDevice& device);
    ~WindowCapture();

    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;
    WindowCapture(WindowCapture&&) = delete;
    WindowCapture& operator=(WindowCapture&&) = delete;

    enum class Result {
        // New pixels are ready to send.
        Fresh,
        // Nothing has changed since the last look. The compositor hands
        // out a frame only when it composed one, so this is the ordinary
        // answer for a window sitting still — and emphatically not a sign
        // that the window has gone.
        Unchanged,
        // The window cannot be captured any more.
        Gone,
    };

    [[nodiscard]] Result grab(const WindowSurface& surface);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::uint8_t* pixels() const noexcept { return visible_.data(); }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return visible_.size(); }

private:
    [[nodiscard]] bool start(const WindowSurface& surface);
    [[nodiscard]] bool resize_pool(const WindowSurface& surface);
    [[nodiscard]] bool read_back(void* frame_texture, const WindowSurface& surface);
    // Closing is what stops the compositor filling the pool. Releasing
    // alone leaves the session running, and the next one cannot be made
    // while the old is still capturing the same window.
    void close_session();
    void stop();

    // Opaque: the capture interfaces are WinRT, and a header that named
    // them would drag the whole projection into everything that mirrors a
    // window.
    const GraphicsDevice& device_;
    void* item_ = nullptr;
    void* pool_ = nullptr;
    void* session_ = nullptr;
    void* staging_ = nullptr;
    std::vector<std::uint8_t> visible_;
    std::uint64_t started_for_ = 0;
    std::uint32_t pool_width_ = 0;
    std::uint32_t pool_height_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

}  // namespace rwin::guest
