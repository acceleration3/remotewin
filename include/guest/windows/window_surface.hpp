#pragma once

#include <cstdint>

namespace rwin::guest {

// A guest window: where its picture is, how big, and whether it is showing.
//
// The picture is what the compositor hands a capture of the window, and it
// is not the window rectangle. Measured on Windows 11 at 120%: an
// overlapped window of 378x285 at 400,300 arrives as 362x277, the size of
// DWMWA_EXTENDED_FRAME_BOUNDS (408,300 to 770,577), at the corner of the
// frame - the invisible resize border, eight pixels left, right and below,
// is not in it at all. A window DWM draws no frame for has bounds equal to
// its rectangle. So every coordinate the host is given or gives back is
// measured from the corner of the extended frame bounds, and the window
// rectangle is used only where Windows wants one: SetWindowRgn's region,
// and SetWindowPos's size.
class WindowSurface {
public:
    explicit WindowSurface(std::uint64_t window_handle);

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    // Nothing to capture, and nowhere meaningful to put a pointer: a
    // minimised window is parked far off-screen.
    [[nodiscard]] bool minimized() const noexcept { return minimized_; }
    // Something Windows.Graphics.Capture can be given: shown, and of some
    // size. It refuses a hidden window with E_INVALIDARG, and WinUI shows
    // its popups at 0x0 before it sizes them and hides them between uses.
    [[nodiscard]] bool capturable() const noexcept { return valid_ && shown_; }
    // Whether a transparent band round the picture is room the window
    // reserved (a WinUI menu leaves ten pixels for a shadow it does not
    // draw) rather than a part of the window that happens to be clear.
    // Neither for a window with a region - the transparency is its shape -
    // nor for one DWM draws a frame for, which is its extended frame
    // bounds whatever its client draws: a captioned
    // WS_EX_NOREDIRECTIONBITMAP window trimmed to its caption took no
    // clicks below it.
    [[nodiscard]] bool margin_is_reserved() const noexcept { return margin_is_reserved_; }

    [[nodiscard]] std::int32_t screen_x(std::int32_t picture_x) const noexcept {
        return left_ + picture_x;
    }
    [[nodiscard]] std::int32_t screen_y(std::int32_t picture_y) const noexcept {
        return top_ + picture_y;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    // Where the picture starts inside the window rectangle: the left and
    // top invisible border.
    [[nodiscard]] std::int32_t inset_x() const noexcept { return inset_x_; }
    [[nodiscard]] std::int32_t inset_y() const noexcept { return inset_y_; }
    // How much larger the window rectangle is than the picture, both sides
    // together: what a picture size is grown by to be a window size.
    [[nodiscard]] std::uint32_t border_width() const noexcept { return border_width_; }
    [[nodiscard]] std::uint32_t border_height() const noexcept { return border_height_; }

    [[nodiscard]] void* handle() const noexcept { return handle_; }

private:
    void* handle_;
    std::int32_t left_ = 0;
    std::int32_t top_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::int32_t inset_x_ = 0;
    std::int32_t inset_y_ = 0;
    std::uint32_t border_width_ = 0;
    std::uint32_t border_height_ = 0;
    bool minimized_ = false;
    bool shown_ = false;
    bool margin_is_reserved_ = false;
    bool valid_ = false;
};

}  // namespace rwin::guest
