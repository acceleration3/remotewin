#pragma once

#include "host/export/unique_fd.hpp"
#include "host/geometry/size.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

struct wl_buffer;
struct wl_shm;

namespace rwin::host {

// A picture in memory shared with the compositor (wl_shm, ARGB8888:
// premultiplied BGRA in memory, top-down), written here and shown there.
// Held by the compositor from the commit it is attached in until it says
// it is done (wl_buffer.release); written again only after.
class ShmBuffer {
public:
    // Throws std::runtime_error when the memory cannot be had.
    ShmBuffer(wl_shm* shm, Size size);
    ~ShmBuffer();

    ShmBuffer(const ShmBuffer&) = delete;
    ShmBuffer& operator=(const ShmBuffer&) = delete;
    ShmBuffer(ShmBuffer&&) = delete;
    ShmBuffer& operator=(ShmBuffer&&) = delete;

    [[nodiscard]] Size size() const noexcept { return size_; }
    [[nodiscard]] int stride() const noexcept { return size_.width * 4; }
    [[nodiscard]] std::span<std::uint8_t> pixels() noexcept { return {pixels_, bytes_}; }
    [[nodiscard]] wl_buffer* buffer() const noexcept { return buffer_; }

    // The compositor has it from the next commit on.
    void attached() noexcept { held_ = true; }
    [[nodiscard]] bool held() const noexcept { return held_; }

private:
    static void released(void* self, wl_buffer* buffer);

    Size size_;
    std::size_t bytes_;
    UniqueFd memory_;
    std::uint8_t* pixels_ = nullptr;
    wl_buffer* buffer_ = nullptr;
    bool held_ = false;
};

}  // namespace rwin::host
