#pragma once

#include "host/latency/latency_meter.hpp"

#include <cstdint>

namespace rwin::host {

struct FrameStamps;

// Where the time of the frames shown went, said in the log every few
// seconds while frames are shown (RWIN_LOG=debug), so a mirror that feels
// slow says which stage it is slow in.
class LatencyLog {
public:
    static constexpr std::int64_t kEveryNs = 5'000'000'000;

    void take(const FrameStamps& frame);

private:
    LatencyMeter meter_;
    std::int64_t since_ = 0;
};

}  // namespace rwin::host
