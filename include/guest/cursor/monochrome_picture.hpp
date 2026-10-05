#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace rwin::guest {

// A monochrome cursor as a picture with alpha.
//
// Its one bitmap is two planes, AND above XOR. Per pixel: AND 0 XOR 0 is
// black, AND 0 XOR 1 white, AND 1 XOR 0 transparent, and AND 1 XOR 1 inverts
// whatever is behind it - which no compositor can be asked to do. Those
// pixels come out opaque black, what they look like over the light
// backgrounds they were designed against, edged in white where they meet
// transparency so that they still show over a dark one - the I-beam over a
// terminal.
struct MonochromePicture {
    // Straight BGRA, top-down, width * 4 per row.
    std::vector<std::uint8_t> bgra;
    // Some pixel inverts, and was drawn as said above.
    bool inverts = false;
};

// ``planes`` is the bitmap as GetDIBits gives it at 32 bits a pixel,
// top-down: ``height`` rows of the AND plane, then as many of the XOR
// plane, a set bit any pixel whose first byte is not 0. Nothing for planes
// shorter than that.
[[nodiscard]] MonochromePicture monochrome_picture(std::span<const std::uint8_t> planes, std::uint32_t width,
                                                   std::uint32_t height);

}  // namespace rwin::guest
