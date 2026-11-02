#pragma once

#include "host/latency/cadence_meter.hpp"
#include "host/latency/latency_meter.hpp"

#include <cstdint>

namespace rwin::host {

struct FrameStamps;

class LatencyLog {
public:
    static constexpr std::int64_t kEveryNs = 5'000'000'000;

    void take(const FrameStamps& frame);

private:
    LatencyMeter meter_;
    CadenceMeter cadence_;
    std::int64_t since_ = 0;
};

}
