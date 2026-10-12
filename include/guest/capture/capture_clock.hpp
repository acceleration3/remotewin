#pragma once

#include <cstdint>

namespace rwin::guest {

// Now, on the clock the compositor stamps captured frames with
// (Direct3D11CaptureFrame::SystemRelativeTime): the performance counter, in
// 100 ns units.
[[nodiscard]] std::int64_t capture_clock_now() noexcept;

// The display's refresh period as the compositor keeps it, in the same
// units; 0 when it will not say.
[[nodiscard]] std::int64_t composition_period() noexcept;

}  // namespace rwin::guest
