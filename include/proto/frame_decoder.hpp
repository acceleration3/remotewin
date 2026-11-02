#pragma once

#include <cstdint>
#include <vector>

namespace rwin {
class FrameDecoder {
public:
    void feed(const std::uint8_t* data, std::size_t size);

    [[nodiscard]] bool next(const std::uint8_t** payload, std::size_t* size);

    static constexpr std::size_t kMaxPayload = 64u << 20;

    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

private:
    std::vector<std::uint8_t> pending_;
    std::size_t consumed_ = 0;
    bool overflowed_ = false;
};

}
