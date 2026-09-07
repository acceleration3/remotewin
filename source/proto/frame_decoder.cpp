#include "proto/frame_decoder.hpp"

namespace rwin {

namespace {
constexpr std::size_t kLengthPrefixSize = 4;

std::uint32_t read_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)
         | static_cast<std::uint32_t>(p[3]);
}
}  // namespace

void FrameDecoder::feed(const std::uint8_t* data, std::size_t size) {
    pending_.insert(pending_.end(), data, data + size);
}

bool FrameDecoder::next(const std::uint8_t** payload, std::size_t* size) {
    // Reclaim what previous calls handed out before looking for more, so
    // a long-lived stream does not grow the buffer without bound.
    if (consumed_ > 0) {
        pending_.erase(pending_.begin(),
                       pending_.begin() + static_cast<std::ptrdiff_t>(consumed_));
        consumed_ = 0;
    }
    if (overflowed_ || pending_.size() < kLengthPrefixSize) {
        return false;
    }
    const std::size_t length = read_u32_be(pending_.data());
    if (length > kMaxPayload) {
        overflowed_ = true;
        return false;
    }
    if (pending_.size() < kLengthPrefixSize + length) {
        return false;
    }
    *payload = pending_.data() + kLengthPrefixSize;
    *size = length;
    consumed_ = kLengthPrefixSize + length;
    return true;
}

}  // namespace rwin
