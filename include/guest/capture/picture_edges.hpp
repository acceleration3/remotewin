#pragma once

#include "guest/capture/capture_stream.hpp"

#include <array>
#include <cstddef>
#include <optional>

struct ID3D11Texture2D;

namespace rwin::guest {

class GraphicsDevice;

// Whether something is drawn on each of a window's frames' four edges, read
// from those edges alone.
//
// A window that leaves a margin round itself for the compositor's shadow is
// trimmed to what it draws (ContentBounds), which takes its pixels. Most
// windows of that kind draw out to every edge anyway - a borderless game is
// one - and then ContentBounds keeps the whole picture: so a frame whose
// edges all hold something stays on the GPU whole, and only one that does
// not is read back to be trimmed. Four lines of pixels are read rather
// than the picture.
//
// Read without waiting for the GPU: each frame's edges are copied out, and
// the verdict is the newest one the GPU has finished - the frame before's,
// as a rule. Only a window's first frame, which has none, waits for its
// own; a verdict a frame late sends at most one frame the other way.
class PictureEdges {
public:
    // Frames whose edges may go unread before one is waited for: a driver
    // that never says it is done without being waited on still answers.
    static constexpr std::size_t kMostUnread = 30;

    explicit PictureEdges(const GraphicsDevice& device) noexcept : device_(device) {}
    ~PictureEdges();

    PictureEdges(const PictureEdges&) = delete;
    PictureEdges& operator=(const PictureEdges&) = delete;
    PictureEdges(PictureEdges&&) = delete;
    PictureEdges& operator=(PictureEdges&&) = delete;

    // False too when the edges cannot be read: the frame is then read back
    // whole, which is never wrong.
    [[nodiscard]] bool all_drawn(const CaptureStream::Frame& frame);

private:
    // The top and bottom rows, the picture's width by 2; the left and
    // right columns, 2 by its height. One pair copied into while the other
    // is read.
    struct Edges {
        ID3D11Texture2D* rows = nullptr;
        ID3D11Texture2D* columns = nullptr;
        bool copied = false;
    };

    [[nodiscard]] bool fit(CaptureStream::Extent size);
    void release() noexcept;
    void copy(Edges& edges, const CaptureStream::Frame& frame);
    // The verdict on ``edges`` once the GPU has copied them; nothing while
    // it has not and ``wait`` is false.
    [[nodiscard]] std::optional<bool> read(Edges& edges, bool wait);
    // Something drawn on both rows, and on both columns: a pixel that is
    // not wholly transparent. Nothing while the GPU is still copying.
    [[nodiscard]] std::optional<bool> rows_drawn(ID3D11Texture2D* rows, bool wait);
    [[nodiscard]] std::optional<bool> columns_drawn(ID3D11Texture2D* columns, bool wait);

    const GraphicsDevice& device_;
    std::array<Edges, 2> edges_{};
    std::size_t next_ = 0;
    CaptureStream::Extent size_;
    std::optional<bool> verdict_;
    std::size_t unread_ = 0;
};

}  // namespace rwin::guest
