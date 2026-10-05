#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rwin {

// Opt-in bounded recording, written only at teardown to avoid per-frame file I/O.
class FrameTiming {
public:
    explicit FrameTiming(const char* stage);
    ~FrameTiming();
    FrameTiming(const FrameTiming&) = delete;
    FrameTiming& operator=(const FrameTiming&) = delete;
    FrameTiming(FrameTiming&&) = delete;
    FrameTiming& operator=(FrameTiming&&) = delete;

    void note(const char* event, std::uint64_t frame = 0,
              std::int64_t source_100ns = 0, std::uint64_t detail = 0);

private:
    struct Sample {
        const char* event;
        std::int64_t local_us;
        std::uint64_t frame;
        std::int64_t source_100ns;
        std::uint64_t detail;
    };
    static constexpr std::size_t kCapacity = 65536;
    std::string path_;
    std::vector<Sample> samples_;
    std::uint64_t overflow_ = 0;
};
}
