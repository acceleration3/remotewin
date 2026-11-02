#pragma once

#include "host/export/unique_fd.hpp"
#include "host/geometry/size.hpp"
#include "host/loop/signal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

struct wl_buffer;
struct wl_shm;

namespace rwin::host {
class ShmBuffer {
public:
    ShmBuffer(wl_shm* shm, Size size, const Signal<>* freed = nullptr);
    ~ShmBuffer();

    ShmBuffer(const ShmBuffer&) = delete;
    ShmBuffer& operator=(const ShmBuffer&) = delete;
    ShmBuffer(ShmBuffer&&) = delete;
    ShmBuffer& operator=(ShmBuffer&&) = delete;

    [[nodiscard]] Size size() const noexcept { return size_; }
    [[nodiscard]] int stride() const noexcept { return size_.width * 4; }
    [[nodiscard]] std::span<std::uint8_t> pixels() noexcept { return {pixels_, bytes_}; }
    [[nodiscard]] wl_buffer* buffer() const noexcept { return buffer_; }

    void attached() noexcept { held_ = true; }
    [[nodiscard]] bool held() const noexcept { return held_; }

private:
    static void released(void* self, wl_buffer* buffer);

    const Signal<>* freed_;
    Size size_;
    std::size_t bytes_;
    UniqueFd memory_;
    std::uint8_t* pixels_ = nullptr;
    wl_buffer* buffer_ = nullptr;
    bool held_ = false;
};

}
