#pragma once

#include "guest/capture/capture_stream.hpp"

struct ID3D11Texture2D;

namespace rwin::guest {

class GraphicsDevice;

// Whether something is drawn on each of a frame's four edges, read from
// those edges alone.
//
// A window that leaves a margin round itself for the compositor's shadow is
// trimmed to what it draws (ContentBounds), which takes its pixels. Most
// windows of that kind draw out to every edge anyway - a borderless game is
// one - and then ContentBounds keeps the whole picture: so a frame whose
// edges all hold something stays on the GPU whole, and only one that does
// not is read back to be trimmed. Four lines of pixels are read rather
// than the picture.
class PictureEdges {
public:
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
    [[nodiscard]] bool fit(CaptureStream::Extent size);
    void release() noexcept;
    // Something drawn on both rows of rows_, and on both columns of
    // columns_: a pixel that is not wholly transparent.
    [[nodiscard]] bool rows_drawn();
    [[nodiscard]] bool columns_drawn();

    const GraphicsDevice& device_;
    // The top and bottom rows, the picture's width by 2; the left and
    // right columns, 2 by its height.
    ID3D11Texture2D* rows_ = nullptr;
    ID3D11Texture2D* columns_ = nullptr;
    CaptureStream::Extent size_;
};

}  // namespace rwin::guest
