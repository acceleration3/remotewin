#pragma once

#include "guest/capture/capture_stream.hpp"

#include <array>
#include <cstddef>
#include <optional>

struct ID3D11Texture2D;

namespace rwin::guest {

class GraphicsDevice;

class PictureEdges {
public:
    static constexpr std::size_t kMostUnread = 30;

    explicit PictureEdges(const GraphicsDevice& device) noexcept : device_(device) {}
    ~PictureEdges();

    PictureEdges(const PictureEdges&) = delete;
    PictureEdges& operator=(const PictureEdges&) = delete;
    PictureEdges(PictureEdges&&) = delete;
    PictureEdges& operator=(PictureEdges&&) = delete;

    [[nodiscard]] bool all_drawn(const CaptureStream::Frame& frame);

private:
    struct Edges {
        ID3D11Texture2D* rows = nullptr;
        ID3D11Texture2D* columns = nullptr;
        bool copied = false;
    };

    [[nodiscard]] bool fit(CaptureStream::Extent size);
    void release() noexcept;
    void copy(Edges& edges, const CaptureStream::Frame& frame);
    [[nodiscard]] std::optional<bool> read(Edges& edges, bool wait);
    [[nodiscard]] std::optional<bool> rows_drawn(ID3D11Texture2D* rows, bool wait);
    [[nodiscard]] std::optional<bool> columns_drawn(ID3D11Texture2D* columns, bool wait);

    const GraphicsDevice& device_;
    std::array<Edges, 2> edges_{};
    std::size_t next_ = 0;
    CaptureStream::Extent size_;
    std::optional<bool> verdict_;
    std::size_t unread_ = 0;
};

}
