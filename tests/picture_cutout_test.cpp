#include "host/wayland/picture_cutout.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::host::Box;
using rwin::host::PictureCutout;
using rwin::host::Size;

constexpr Size kSize{4, 3};
constexpr int kStride = 4 * 4 + 8;  // rows padded, as a buffer's are

std::vector<std::uint8_t> picture(std::array<std::uint8_t, 4> texel) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kStride) * kSize.height, 0xEE);
    for (int row = 0; row < kSize.height; ++row) {
        for (int column = 0; column < kSize.width; ++column) {
            for (int byte = 0; byte < 4; ++byte) pixels[row * kStride + column * 4 + byte] = texel[byte];
        }
    }
    return pixels;
}

bool clear_at(const std::vector<std::uint8_t>& pixels, int column, int row) {
    const std::uint8_t* texel = &pixels[row * kStride + column * 4];
    return texel[0] == 0 && texel[1] == 0 && texel[2] == 0 && texel[3] == 0;
}

void a_colour_key_is_cut_out() {
    // COLORREF 0x00BBGGRR: red 0x10, green 0x20, blue 0x30.
    std::vector<std::uint8_t> pixels = picture({0x30, 0x20, 0x10, 0xFF});
    pixels[1 * kStride + 2 * 4 + 1] = 0x21;  // one texel a shade off the key
    PictureCutout{0x00302010u, {}}.apply(pixels, kStride, kSize);
    check(clear_at(pixels, 0, 0) && clear_at(pixels, 3, 2), "the key's texels are transparent");
    check(!clear_at(pixels, 2, 1), "a texel not quite the key stays");
    check(pixels[kSize.width * 4] == 0xEE, "a row's padding is left alone");
}

void outside_the_region_is_cut_out() {
    std::vector<std::uint8_t> pixels = picture({1, 2, 3, 0xFF});
    PictureCutout{std::nullopt, {Box{1, 0, 2, 1}, Box{0, 2, 9, 9}}}.apply(pixels, kStride, kSize);
    check(clear_at(pixels, 0, 0) && clear_at(pixels, 3, 0), "beside the first rectangle is cut");
    check(!clear_at(pixels, 1, 0) && !clear_at(pixels, 2, 0), "the first rectangle stays");
    check(clear_at(pixels, 0, 1) && clear_at(pixels, 3, 1), "a row no rectangle crosses is cut whole");
    check(!clear_at(pixels, 0, 2) && !clear_at(pixels, 3, 2), "a rectangle past the picture keeps its part");
}

void none_is_nothing() {
    std::vector<std::uint8_t> pixels = picture({1, 2, 3, 0xFF});
    const std::vector<std::uint8_t> before = pixels;
    const PictureCutout none;
    check(none.none(), "no key and no region is no cutout");
    none.apply(pixels, kStride, kSize);
    check(pixels == before, "and changes nothing");
}

}  // namespace

int main() {
    a_colour_key_is_cut_out();
    outside_the_region_is_cut_out();
    none_is_nothing();
    if (failures == 0) std::printf("picture_cutout: ok\n");
    return failures == 0 ? 0 : 1;
}
