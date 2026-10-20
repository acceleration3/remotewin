#pragma once

#include "host/export/surface_memory.hpp"
#include "host/geometry/size.hpp"
#include "host/latency/frame_stamps.hpp"

#include <cstdint>
#include <memory>

namespace rwin::host {

class SlotLease;

// A guest window's frame that stayed on the GPU: the memory it is in, the
// fence that signals once the guest's copy into it is done there, and the
// lease on its slot, which keeps the guest from copying another frame in
// while this one is read. The picture is at the memory's top left, and can
// be smaller than it: a ring outlasts a window's resizes.
struct SharedFrame {
    std::shared_ptr<const SurfaceMemory> memory;
    std::shared_ptr<const SlotLease> lease;
    // The sync_file; whoever first draws the frame waits on it and takes it.
    std::shared_ptr<UniqueFd> fence;
    std::uint32_t ring = 0;
    std::uint64_t order = 0;
    Size picture;
    FrameStamps stamps;

    [[nodiscard]] Size size() const noexcept { return picture; }
};

}  // namespace rwin::host
