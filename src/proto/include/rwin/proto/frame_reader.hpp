#pragma once

#include "rwin/proto/envelope.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rwin::proto {

// Stream reassembler for the envelope framing. Both ends of the link
// receive a byte stream and need the same loop: append what recv gave
// us, hand out every complete envelope, keep the remainder.
//
// Not thread-safe; each connection owns one.
class FrameReader {
public:
    void append(const std::uint8_t* data, std::size_t len) {
        buf_.insert(buf_.end(), data, data + len);
    }

    // Invoke ``on_frame(FrameType, const std::uint8_t* payload, std::size_t
    // payload_len)`` for each complete envelope buffered. The payload
    // pointer is valid only for the duration of the call.
    //
    // Stops and returns false the moment ``on_frame`` returns false — the
    // caller is tearing the connection down, and the consumed prefix is
    // still dropped so a resumed reader would not see it twice.
    template <typename Fn>
    [[nodiscard]] bool drain(Fn&& on_frame) {
        std::size_t pos = 0;
        bool keep_going = true;
        for (;;) {
            if (buf_.size() - pos < kHeaderSize) {
                break;
            }
            const std::uint8_t* header = buf_.data() + pos;
            const std::uint32_t len = read_payload_len(header);
            if (buf_.size() - pos < kHeaderSize + len) {
                break;
            }
            const FrameType type = read_type(header);
            const std::uint8_t* payload = header + kHeaderSize;
            pos += kHeaderSize + len;
            if (!on_frame(type, payload, static_cast<std::size_t>(len))) {
                keep_going = false;
                break;
            }
        }
        if (pos > 0) {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos));
        }
        return keep_going;
    }

    // Drop everything buffered — used when the connection is dropped and
    // a half-received envelope must not be prepended to the next one.
    void reset() noexcept { buf_.clear(); }

    [[nodiscard]] std::size_t buffered() const noexcept { return buf_.size(); }

    void reserve(std::size_t bytes) { buf_.reserve(bytes); }

private:
    std::vector<std::uint8_t> buf_;
};

}  // namespace rwin::proto
