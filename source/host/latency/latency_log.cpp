#include "host/latency/latency_log.hpp"

#include "host/latency/frame_stamps.hpp"
#include "logging/logger.hpp"

namespace rwin::host {

void LatencyLog::take(const FrameStamps& frame) {
    const std::int64_t now = now_ns();
    if (since_ == 0) since_ = now;
    meter_.take(frame);
    if (now - since_ < kEveryNs) return;
    RWIN_DEBUG("latency", "{}", meter_.summary());
    meter_.restart();
    since_ = now;
}

}  // namespace rwin::host
