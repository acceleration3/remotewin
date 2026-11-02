#pragma once

#include "logging/frame_timing.hpp"

#include <cstdint>
#include <optional>
#include <vector>

struct ID3D11Texture2D;

namespace rwin::guest {

class CaptureCloser;
class FrameSignal;
class GraphicsDevice;

class CaptureStream {
public:
    struct Extent {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        [[nodiscard]] bool operator==(const Extent&) const noexcept = default;
    };

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
        [[nodiscard]] std::int64_t taken_time() const noexcept { return taken_time_; }

    private:
        friend class CaptureStream;

        void let_go() noexcept;

        void* frame_;
        ID3D11Texture2D* texture_;
        Extent content_;
        std::int64_t source_time_ = 0;
        std::int64_t taken_time_ = 0;
    };

    CaptureStream(const GraphicsDevice& device, const FrameSignal& arrivals, CaptureCloser& closer);
    ~CaptureStream();

    CaptureStream(const CaptureStream&) = delete;
    CaptureStream& operator=(const CaptureStream&) = delete;
    CaptureStream(CaptureStream&&) = delete;
    CaptureStream& operator=(CaptureStream&&) = delete;

    [[nodiscard]] bool open_window(void* window, Extent size);
    [[nodiscard]] bool open_monitor(void* monitor, Extent size);

    [[nodiscard]] bool resize(Extent size);
    [[nodiscard]] bool capture_afresh(Extent size);
    [[nodiscard]] Extent size() const noexcept { return size_; }
    [[nodiscard]] const std::optional<Extent>& outgrown() const noexcept { return outgrown_; }

    [[nodiscard]] std::optional<Extent> read_next(std::vector<std::uint8_t>& into);

    [[nodiscard]] std::optional<Frame> next_frame();
    [[nodiscard]] std::optional<Extent> read(const Frame& frame, std::vector<std::uint8_t>& into);

    void close();

private:
    [[nodiscard]] bool open(void* item, Extent size);
    [[nodiscard]] bool start(Extent size);
    [[nodiscard]] bool recreate(Extent size);
    void close_capture();
    void sample_frame(const Frame& frame);

    const GraphicsDevice& device_;
    const FrameSignal& arrivals_;
    CaptureCloser& closer_;
    void* item_ = nullptr;
    void* pool_ = nullptr;
    void* session_ = nullptr;
    void* staging_ = nullptr;
    std::optional<std::int64_t> arrival_token_;
    Extent size_;
    std::optional<Extent> outgrown_;
    FrameTiming timing_{"capture"};
    std::uint64_t captured_ = 0;
    void* sample_ = nullptr;
    bool sample_pixels_ = false;
};

}
