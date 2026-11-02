#pragma once

#include "host/export/exported_layout.hpp"
#include "host/export/unique_fd.hpp"

#include <cstdint>

namespace rwin::host {
class SurfaceMemory {
public:
    SurfaceMemory(std::uint64_t surface, ExportedLayout layout, UniqueFd dma_buf) noexcept
        : surface_(surface), layout_(layout), dma_buf_(std::move(dma_buf)) {}

    [[nodiscard]] std::uint64_t surface() const noexcept { return surface_; }
    [[nodiscard]] const ExportedLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] int dma_buf() const noexcept { return dma_buf_.get(); }

private:
    std::uint64_t surface_;
    ExportedLayout layout_;
    UniqueFd dma_buf_;
};

}
