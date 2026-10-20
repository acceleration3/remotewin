#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::host {
class BusConnection;
}

namespace rwin::integration {

// The test compositor's screen as it composed it: an area read through
// KWin's ScreenShot2 (CaptureArea) on the harness's private bus, in device
// pixels. What a window looks like on screen - a corner clear or drawn
// black - is said by nothing else: neither KWin's scripting nor the host
// sees what the compositor made of a buffer.
class ScreenCapture {
public:
    // Device pixels, top-down, each 0xAARRGGBB.
    struct Picture {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;

        [[nodiscard]] std::uint32_t at(int x, int y) const noexcept;
    };

    explicit ScreenCapture(host::BusConnection& bus) : bus_(bus) {}

    // The area of ``width`` by ``height`` logical units from (x, y), at the
    // output's own resolution; nothing when KWin will not say.
    [[nodiscard]] std::optional<Picture> area(int x, int y, int width, int height);

private:
    host::BusConnection& bus_;
};

}  // namespace rwin::integration
