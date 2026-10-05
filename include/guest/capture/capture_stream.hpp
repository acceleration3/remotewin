#pragma once

#include "logging/frame_timing.hpp"

#include <cstdint>
#include <optional>
#include <vector>

struct ID3D11Texture2D;

namespace rwin::guest {

class FrameSignal;
class GraphicsDevice;

// Frames of one thing on screen, as the compositor composed them
// (Windows.Graphics.Capture): a window, or a whole monitor.
//
// Taken from the compositor rather than asking anything to draw itself.
// PrintWindow goes through GDI, which has no alpha channel: every pixel
// comes back opaque, so a rounded corner is a black notch, a shadow is a
// black band, and a window that is meant to be see-through is not. What
// DWM already composed has all of that, and this asks for a copy of it.
//
// The guest's cursor is left out of every frame: it belongs to the desktop
// rather than to anything captured, and the host draws it as its own.
class CaptureStream {
public:
    struct Extent {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        [[nodiscard]] bool operator==(const Extent&) const noexcept = default;
    };

    // One frame as the compositor handed it over, held until this is gone:
    // the pool does not compose into a buffer while its frame is held.
    // ``content`` is the part of the texture that is the picture, from its
    // top left; the rest is whatever the pool's buffer last held.
    class Frame {
    public:
        Frame(void* frame, ID3D11Texture2D* texture, Extent content) noexcept
            : frame_(frame), texture_(texture), content_(content) {}
        ~Frame();

        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        Frame(Frame&& other) noexcept;
        Frame& operator=(Frame&& other) noexcept;

        [[nodiscard]] ID3D11Texture2D* texture() const noexcept { return texture_; }
        [[nodiscard]] Extent content() const noexcept { return content_; }
        [[nodiscard]] std::int64_t source_time() const noexcept { return source_time_; }

    private:
        friend class CaptureStream;

        void let_go() noexcept;

        void* frame_;
        ID3D11Texture2D* texture_;
        Extent content_;
        std::int64_t source_time_ = 0;
    };

    // ``arrivals`` is raised each time the compositor has a frame here.
    CaptureStream(const GraphicsDevice& device, const FrameSignal& arrivals);
    ~CaptureStream();

    CaptureStream(const CaptureStream&) = delete;
    CaptureStream& operator=(const CaptureStream&) = delete;
    CaptureStream(CaptureStream&&) = delete;
    CaptureStream& operator=(CaptureStream&&) = delete;

    // Captures ``window`` (an HWND) or ``monitor`` (an HMONITOR) from here
    // on, in frames ``size`` large, whatever was captured before. False,
    // and nothing captured, when the compositor will not.
    [[nodiscard]] bool open_window(void* window, Extent size);
    [[nodiscard]] bool open_monitor(void* monitor, Extent size);

    // Frames of another size from here on: what is captured has changed
    // size, and frames of the old one would crop it or pad it.
    [[nodiscard]] bool resize(Extent size);
    [[nodiscard]] Extent size() const noexcept { return size_; }

    // The next frame the compositor has, if it has one: its content, from
    // the top left of the buffer, copied into ``into`` with its rows packed
    // (BGRA, premultiplied, width * 4 bytes per row), and how large it is.
    [[nodiscard]] std::optional<Extent> read_next(std::vector<std::uint8_t>& into);

    // The newest frame the compositor has, left on the GPU; nothing when it
    // has none, or none with any picture in it. Older ones waiting behind
    // it are let go: a picture already replaced is not worth sending.
    [[nodiscard]] std::optional<Frame> next_frame();
    // ``frame``'s picture read back as read_next reads one.
    [[nodiscard]] std::optional<Extent> read(const Frame& frame, std::vector<std::uint8_t>& into);

    void close();

private:
    [[nodiscard]] bool open(void* item, Extent size);
    // Closing is what stops the compositor filling the pool. Releasing
    // alone leaves the session running, and the next one cannot be made
    // while the old is still capturing the same thing.
    void close_session();
    void close_pool();
    void sample_frame(const Frame& frame);

    // Opaque: the capture interfaces are WinRT, and a header that named
    // them would drag the whole projection into everything that captures.
    const GraphicsDevice& device_;
    const FrameSignal& arrivals_;
    void* item_ = nullptr;
    void* pool_ = nullptr;
    void* session_ = nullptr;
    void* staging_ = nullptr;
    // The pool's FrameArrived registration, while it has one.
    std::optional<std::int64_t> arrival_token_;
    Extent size_;
    FrameTiming timing_{"capture"};
    std::uint64_t captured_ = 0;
    void* sample_ = nullptr;
    bool sample_pixels_ = false;
};

}  // namespace rwin::guest
