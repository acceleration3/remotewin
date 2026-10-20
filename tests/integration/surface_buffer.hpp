#pragma once

#include "host/export/unique_fd.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rwin::integration {

// One shared surface's memory as the played GPU device keeps it: memory
// this process writes the played guest's copies into, handed to the host
// as a dma-buf a compositor can show (a udmabuf of it) - or as the memory
// alone where /dev/udmabuf cannot be opened, which reaches the host's
// mirror and goes no further.
//
// Rows are 256 bytes apart at least: a GPU imports a linear buffer only at
// a pitch it can sample.
class SurfaceBuffer {
public:
    // ``udmabuf`` is /dev/udmabuf, opened; -1 where it could not be.
    SurfaceBuffer(int udmabuf, std::uint32_t width, std::uint32_t height);
    ~SurfaceBuffer();

    SurfaceBuffer(const SurfaceBuffer&) = delete;
    SurfaceBuffer& operator=(const SurfaceBuffer&) = delete;
    SurfaceBuffer(SurfaceBuffer&& other) noexcept;
    SurfaceBuffer& operator=(SurfaceBuffer&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept { return mapped_ != nullptr; }
    // What the host is handed for it: the dma-buf, else the memory.
    [[nodiscard]] int handed() const noexcept { return dma_buf_.valid() ? dma_buf_.get() : memory_.get(); }
    [[nodiscard]] bool showable() const noexcept { return dma_buf_.valid(); }
    [[nodiscard]] std::uint32_t stride() const noexcept { return stride_; }

    // A picture copied to the top left: packed premultiplied BGRA rows,
    // ``width`` pixels each.
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

}  // namespace rwin::integration
