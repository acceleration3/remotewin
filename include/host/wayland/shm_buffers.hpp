#pragma once

#include "host/geometry/size.hpp"
#include "host/loop/signal.hpp"
#include "host/wayland/shm_buffer.hpp"

#include <memory>
#include <vector>

struct wl_shm;

namespace rwin::host {
class ShmBuffers {
public:
    explicit ShmBuffers(wl_shm* shm) noexcept : shm_(shm) {}

    [[nodiscard]] ShmBuffer* free_buffer(Size size);

    Signal<> freed;

private:
    static constexpr std::size_t kMost = 3;

    wl_shm* shm_;
    std::vector<std::unique_ptr<ShmBuffer>> buffers_;
};

}
