#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::probe {

// The picture a probe window paints in its client area.
//
// Made to be compared, not looked at: four flat quadrants in colours far
// apart, so a picture shifted or cropped shows up as a band of the wrong
// colour along an edge; a strip of one-pixel checks along the top, so any
// resampling turns it grey; and, for a colour-keyed window, a patch of the
// key in the middle, so there is something for the key to remove. No
// text, because how text renders is the font's business, not the mirror's.
class TestPattern {
public:
    // ``key`` is the COLORREF to paint the middle patch in, or nothing.
    void paint(void* device_context, std::int32_t width, std::int32_t height,
               std::optional<std::uint32_t> key);

private:
    void fill(std::int32_t width, std::int32_t height, std::optional<std::uint32_t> key);

    // 0x00RRGGBB per pixel, top-down: a DIB's own layout. Kept, so a
    // repaint at the same size allocates nothing.
    std::vector<std::uint32_t> pixels_;
};

}  // namespace rwin::probe
