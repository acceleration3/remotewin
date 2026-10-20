#include "surface_buffer.hpp"

#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace rwin::integration {

namespace {

constexpr std::uint32_t kPitchAlignment = 256;
constexpr std::size_t kBytesPerPixel = 4;

std::size_t page_rounded(std::size_t bytes) {
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    return (bytes + page - 1) / page * page;
}

}  // namespace

SurfaceBuffer::SurfaceBuffer(int udmabuf, std::uint32_t width, std::uint32_t height)
    : stride_((width * kBytesPerPixel + kPitchAlignment - 1) / kPitchAlignment * kPitchAlignment), height_(height) {
    bytes_ = page_rounded(static_cast<std::size_t>(stride_) * height);
    memory_ = host::UniqueFd{::memfd_create("rwin-integration-surface", MFD_CLOEXEC | MFD_ALLOW_SEALING)};
    if (!memory_.valid() || ::ftruncate(memory_.get(), static_cast<off_t>(bytes_)) != 0) {
        std::fprintf(stderr, "export: no memory for a %ux%u surface: %s\n", width, height, std::strerror(errno));
        return;
    }
    void* mapped = ::mmap(nullptr, bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, memory_.get(), 0);
    if (mapped == MAP_FAILED) {
        std::fprintf(stderr, "export: the %ux%u surface's memory cannot be mapped: %s\n", width, height,
                     std::strerror(errno));
        return;
    }
    mapped_ = static_cast<std::uint8_t*>(mapped);
    if (udmabuf < 0 || ::fcntl(memory_.get(), F_ADD_SEALS, F_SEAL_SHRINK) != 0) return;
    udmabuf_create made{};
    made.memfd = static_cast<__u32>(memory_.get());
    made.flags = UDMABUF_FLAGS_CLOEXEC;
    made.offset = 0;
    made.size = bytes_;
    dma_buf_ = host::UniqueFd{::ioctl(udmabuf, UDMABUF_CREATE, &made)};
    if (!dma_buf_.valid()) {
        std::fprintf(stderr, "export: no dma-buf of a %ux%u surface: %s\n", width, height, std::strerror(errno));
    }
}

SurfaceBuffer::~SurfaceBuffer() { unmap(); }

SurfaceBuffer::SurfaceBuffer(SurfaceBuffer&& other) noexcept
    : memory_(std::move(other.memory_)), dma_buf_(std::move(other.dma_buf_)),
      mapped_(std::exchange(other.mapped_, nullptr)), bytes_(std::exchange(other.bytes_, 0)), stride_(other.stride_),
      height_(other.height_) {}

SurfaceBuffer& SurfaceBuffer::operator=(SurfaceBuffer&& other) noexcept {
    if (this == &other) return *this;
    unmap();
    memory_ = std::move(other.memory_);
    dma_buf_ = std::move(other.dma_buf_);
    mapped_ = std::exchange(other.mapped_, nullptr);
    bytes_ = std::exchange(other.bytes_, 0);
    stride_ = other.stride_;
    height_ = other.height_;
    return *this;
}

void SurfaceBuffer::unmap() noexcept {
    if (mapped_ != nullptr) ::munmap(mapped_, bytes_);
    mapped_ = nullptr;
}

void SurfaceBuffer::copy(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height) {
    if (mapped_ == nullptr) return;
    const std::size_t row = std::min<std::size_t>(static_cast<std::size_t>(width) * kBytesPerPixel, stride_);
    for (std::uint32_t y = 0; y < std::min(height, height_); ++y) {
        const std::size_t from = static_cast<std::size_t>(y) * width * kBytesPerPixel;
        if (from + row > pixels.size()) return;
        std::memcpy(mapped_ + static_cast<std::size_t>(y) * stride_, pixels.data() + from, row);
    }
}

}  // namespace rwin::integration
