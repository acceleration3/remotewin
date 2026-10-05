#pragma once

namespace rwin::guest {

// What a look at a capture found.
enum class CaptureResult {
    // New pixels are ready to send.
    Fresh,
    // Nothing has changed since the last look. The compositor hands out a
    // frame only when it composed one, so this is the ordinary answer for
    // a picture sitting still - and emphatically not a sign that what is
    // captured has gone.
    Unchanged,
    // It cannot be captured any more.
    Gone,
};

}  // namespace rwin::guest
