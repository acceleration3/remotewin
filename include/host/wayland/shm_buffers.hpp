#pragma once

#include "host/geometry/size.hpp"
#include "host/loop/signal.hpp"
#include "host/wayland/shm_buffer.hpp"

#include <memory>
#include <vector>

struct wl_shm;

namespace rwin::host {

// One surface's pictures in shared memory, made once and written again
// frame after frame: a buffer the compositor has let go of is the next
// one written. A few at most - one shown, one on its way, one being
// written - and none of a size no longer shown.
class ShmBuffers {
public:
    explicit ShmBuffers(wl_shm* shm) noexcept : shm_(shm) {}

    // One of ``size`` the compositor does not hold; null when it holds every
    // one there may be, and the frame waits.
    [[nodiscard]] ShmBuffer* free_buffer(Size size);

    // The compositor let go of one. Told from its release event, so whoever
    // hears it writes no buffer then and there: one could be freed under
    // the event.
    Signal<> freed;

private:
    static constexpr std::size_t kMost = 3;

    wl_shm* shm_;
    std::vector<std::unique_ptr<ShmBuffer>> buffers_;
};

}  // namespace rwin::host
