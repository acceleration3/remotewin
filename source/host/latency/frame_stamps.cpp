#include "host/latency/frame_stamps.hpp"

#include <ctime>

namespace rwin::host {

namespace {
constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
}

std::int64_t now_ns() noexcept {
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * kNanosecondsPerSecond + now.tv_nsec;
}

const char* to_string(FrameStamps::Stage stage) noexcept {
    switch (stage) {
        case FrameStamps::Stage::Composed:
            return "composed";
        case FrameStamps::Stage::Taken:
            return "taken";
        case FrameStamps::Stage::Copied:
            return "copied";
        case FrameStamps::Stage::Received:
            return "received";
        case FrameStamps::Stage::Executed:
            return "executed";
        case FrameStamps::Stage::Ready:
            return "ready";
        case FrameStamps::Stage::Shown:
            return "shown";
        case FrameStamps::Stage::Acquired:
            return "acquired";
        case FrameStamps::Stage::Committed:
            return "committed";
        case FrameStamps::Stage::Presented:
            return "presented";
    }
    return "?";
}

}
