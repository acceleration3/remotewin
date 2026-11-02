#include "host/latency/latency_log.hpp"

#include "host/latency/frame_stamps.hpp"
#include "logging/logger.hpp"

#include <format>
#include <string>

namespace rwin::host {

namespace {

std::string stages_of(const FrameStamps& frame) {
    std::string line;
    std::int64_t last = 0;
    for (std::size_t stage = 0; stage < FrameStamps::kStages; ++stage) {
        const std::int64_t when = frame.at[stage];
        if (when == 0) continue;
        if (last != 0) {
            line += std::format("{}{} {:.2f}", line.empty() ? "" : ", ",
                                to_string(static_cast<FrameStamps::Stage>(stage)),
                                static_cast<double>(when - last) / 1e6);
        }
        last = when;
    }
    return line;
}

}

void LatencyLog::take(const FrameStamps& frame) {
    const std::int64_t now = now_ns();
    if (since_ == 0) since_ = now;
    meter_.take(frame);
    const std::size_t held = cadence_.held();
    cadence_.take(frame);
    if (cadence_.held() != held) RWIN_DEBUG("latency", "held a refresh: {} ms", stages_of(frame));
    if (now - since_ < kEveryNs) return;
    RWIN_DEBUG("latency", "{}; {}", meter_.summary(), cadence_.summary());
    meter_.restart();
    cadence_.restart();
    since_ = now;
}

}
