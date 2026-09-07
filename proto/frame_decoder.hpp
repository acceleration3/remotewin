#pragma once

#include <cstdint>
#include <vector>

namespace rwin {

// Turns a byte stream back into whole messages.
//
// A socket read is not a message: it can hold a fragment, several, or a
// message and part of the next. This owns the pending bytes and yields a
// payload only once all of it has arrived.
class FrameDecoder {
public:
    void feed(const std::uint8_t* data, std::size_t size);

    // The next whole message payload, or empty when more bytes are
    // needed. The span points into this decoder and is invalidated by
    // the next call to either method.
    [[nodiscard]] bool next(const std::uint8_t** payload, std::size_t* size);

    // A peer that claims a message larger than this is not framing
    // correctly, and believing it would let it name our memory footprint.
    static constexpr std::size_t kMaxPayload = 64u << 20;

    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

private:
    std::vector<std::uint8_t> pending_;
    std::size_t consumed_ = 0;
    bool overflowed_ = false;
};

}  // namespace rwin
