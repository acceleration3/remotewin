#pragma once

#include "host/export/surface_memory.hpp"
#include "host/geometry/size.hpp"
#include "host/latency/frame_stamps.hpp"

#include <cstdint>
#include <memory>

namespace rwin::host {

class SlotLease;

struct SharedFrame {
    std::shared_ptr<const SurfaceMemory> memory;
    std::shared_ptr<const SlotLease> lease;
    std::shared_ptr<UniqueFd> fence;
    std::uint32_t ring = 0;
    std::uint64_t order = 0;
    Size picture;
    FrameStamps stamps;

    [[nodiscard]] Size size() const noexcept { return picture; }
};

}
