#include "logging/frame_timing.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace rwin {

FrameTiming::FrameTiming(const char* stage) {
    const char* directory = std::getenv("RWIN_TIMING_DIR");
    if (directory == nullptr || *directory == '\0') return;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = (std::filesystem::path(directory) /
             (std::string(stage) + "-" + std::to_string(stamp) + ".csv")).string();
    samples_.reserve(kCapacity);
}

FrameTiming::~FrameTiming() {
    if (path_.empty()) return;
    FILE* file = std::fopen(path_.c_str(), "w");
    if (file == nullptr) {
        std::fprintf(stderr, "Cannot write frame timing to %s\n", path_.c_str());
        return;
    }
    std::fprintf(file, "event,local_us,frame,source_100ns,detail\n");
    for (const Sample& sample : samples_) {
        std::fprintf(file, "%s,%lld,%llu,%lld,%llu\n", sample.event,
                     static_cast<long long>(sample.local_us),
                     static_cast<unsigned long long>(sample.frame),
                     static_cast<long long>(sample.source_100ns),
                     static_cast<unsigned long long>(sample.detail));
    }
    std::fprintf(file, "overflow,0,0,0,%llu\n", static_cast<unsigned long long>(overflow_));
    std::fclose(file);
}

void FrameTiming::note(const char* event, std::uint64_t frame,
                      std::int64_t source_100ns, std::uint64_t detail) {
    if (path_.empty()) return;
    if (samples_.size() == kCapacity) {
        ++overflow_;
        return;
    }
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    samples_.push_back({event, now, frame, source_100ns, detail});
}
}
