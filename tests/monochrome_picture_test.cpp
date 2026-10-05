#include "guest/cursor/monochrome_picture.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using rwin::guest::monochrome_picture;
using rwin::guest::MonochromePicture;

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

constexpr std::size_t kBytesPerPixel = 4;

// The two planes as GetDIBits gives them at 32 bits a pixel: a set bit is
// white, 0xFFFFFF.
std::vector<std::uint8_t> planes_of(const std::vector<int>& and_bits, const std::vector<int>& xor_bits) {
    std::vector<std::uint8_t> planes;
    for (const std::vector<int>* plane : {&and_bits, &xor_bits}) {
        for (const int bit : *plane) {
            const std::uint8_t shade = bit != 0 ? 0xFF : 0x00;
            planes.insert(planes.end(), {shade, shade, shade, 0});
        }
    }
    return planes;
}

std::array<std::uint8_t, kBytesPerPixel> pixel(const MonochromePicture& picture, std::size_t at) {
    return {picture.bgra[at * 4], picture.bgra[at * 4 + 1], picture.bgra[at * 4 + 2], picture.bgra[at * 4 + 3]};
}

constexpr std::array<std::uint8_t, kBytesPerPixel> kBlack{0, 0, 0, 0xFF};
constexpr std::array<std::uint8_t, kBytesPerPixel> kWhite{0xFF, 0xFF, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, kBytesPerPixel> kClear{0, 0, 0, 0};

void the_four_combinations() {
    const MonochromePicture picture = monochrome_picture(planes_of({0, 0, 1, 1}, {0, 1, 0, 1}), 4, 1);
    check(picture.bgra.size() == 16, "one row of four pixels");
    check(pixel(picture, 0) == kBlack, "AND 0 XOR 0 is black");
    check(pixel(picture, 1) == kWhite, "AND 0 XOR 1 is white");
    check(pixel(picture, 2) == kWhite, "AND 1 XOR 0 is transparent - but beside an inverting pixel, edged white");
    check(pixel(picture, 3) == kBlack, "AND 1 XOR 1 inverts, and is drawn black");
    check(picture.inverts, "and the picture says it inverts");
}

void an_inverting_pixel_is_ringed_with_white() {
    // 3x3, the middle inverting, the rest transparent.
    const MonochromePicture picture =
        monochrome_picture(planes_of({1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 1, 0, 0, 0, 0}), 3, 3);
    check(pixel(picture, 4) == kBlack, "the middle is black");
    for (const std::size_t side : {1U, 3U, 5U, 7U}) check(pixel(picture, side) == kWhite, "its sides are white");
    for (const std::size_t corner : {0U, 2U, 6U, 8U}) check(pixel(picture, corner) == kClear, "its corners clear");
}

void a_plain_monochrome_cursor_is_not_edged() {
    const MonochromePicture picture = monochrome_picture(planes_of({1, 0, 1}, {0, 0, 0}), 3, 1);
    check(!picture.inverts, "nothing inverts");
    check(pixel(picture, 0) == kClear && pixel(picture, 1) == kBlack && pixel(picture, 2) == kClear,
          "transparent beside black stays transparent");
}

void short_planes_are_no_picture() {
    const std::vector<std::uint8_t> planes = planes_of({1, 1}, {0});
    check(monochrome_picture(planes, 2, 1).bgra.empty(), "a bitmap shorter than its two planes is refused");
    check(monochrome_picture({}, 0, 0).bgra.empty(), "and so is an empty one");
}

}  // namespace

int main() {
    the_four_combinations();
    an_inverting_pixel_is_ringed_with_white();
    a_plain_monochrome_cursor_is_not_edged();
    short_planes_are_no_picture();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
