#pragma once

#include "host/export/unique_fd.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rwin::integration {
class SurfaceBuffer {
public:
    SurfaceBuffer(int udmabuf, std::uint32_t width, std::uint32_t height);
    ~SurfaceBuffer();

    SurfaceBuffer(const SurfaceBuffer&) = delete;
    SurfaceBuffer& operator=(const SurfaceBuffer&) = delete;
    SurfaceBuffer(SurfaceBuffer&& other) noexcept;
    SurfaceBuffer& operator=(SurfaceBuffer&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept { return mapped_ != nullptr; }
    [[nodiscard]] int handed() const noexcept { return dma_buf_.valid() ? dma_buf_.get() : memory_.get(); }
    [[nodiscard]] bool showable() const noexcept { return dma_buf_.valid(); }
    [[nodiscard]] std::uint32_t stride() const noexcept { return stride_; }

    void copy(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height);

private:
    void unmap() noexcept;

    host::UniqueFd memory_;
    host::UniqueFd dma_buf_;
    std::uint8_t* mapped_ = nullptr;
    std::size_t bytes_ = 0;
    std::uint32_t stride_ = 0;
    std::uint32_t height_ = 0;
};

}
