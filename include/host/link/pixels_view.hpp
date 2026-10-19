#pragma once

#include <cstdint>

namespace rwin::host {

// A guest picture as it lies in the message that brought it: BGRA with
// premultiplied alpha, as the guest's compositor composed it, top-down.
// Valid only while the message is being handled: whoever keeps it copies.
struct PixelsView {
    const std::uint8_t* bgra = nullptr;
    int width = 0;
    int height = 0;
    // Bytes from one row to the next.
    int stride = 0;
};

}  // namespace rwin::host
