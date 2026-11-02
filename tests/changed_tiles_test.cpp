#include "guest/capture/changed_tiles.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

constexpr std::uint32_t kWidth = 200;
constexpr std::uint32_t kHeight = 150;

void paint(std::vector<std::uint8_t>& picture, std::uint32_t x, std::uint32_t y, std::uint8_t value) {
    picture[(static_cast<std::size_t>(y) * kWidth + x) * 4] = value;
}

}

int main() {
    using rwin::PictureRect;
    using rwin::guest::ChangedTiles;
    constexpr std::uint32_t kTile = ChangedTiles::kTile;

    std::vector<std::uint8_t> picture(static_cast<std::size_t>(kWidth) * kHeight * 4, 0);
    ChangedTiles tiles;
    check(!tiles.weigh(picture.data(), kWidth, kHeight), "the first picture is sent whole");
    check(tiles.weigh(picture.data(), kWidth, kHeight) && tiles.changed().empty(),
          "the same picture again changes nothing");

    paint(picture, 70, 10, 1);
    paint(picture, 130, 20, 1);
    paint(picture, 199, 149, 1);
    check(tiles.weigh(picture.data(), kWidth, kHeight), "a few pixels are a patch");
    const std::vector<PictureRect> expected{{kTile, 0, 2 * kTile, kTile},
                                            {3 * kTile, 2 * kTile, kWidth - 3 * kTile, kHeight - 2 * kTile}};
    check(tiles.changed() == expected, "changed tiles side by side are one rectangle, edge tiles clipped");
    check(tiles.weigh(picture.data(), kWidth, kHeight) && tiles.changed().empty(),
          "a change patched is the picture kept");

    for (std::uint32_t y = 0; y < kHeight; ++y) paint(picture, 5, y, 2);
    for (std::uint32_t y = 0; y < kHeight; ++y) paint(picture, 100, y, 2);
    for (std::uint32_t y = 0; y < kHeight; ++y) paint(picture, 150, y, 2);
    check(!tiles.weigh(picture.data(), kWidth, kHeight), "a picture mostly changed is sent whole");
    check(tiles.weigh(picture.data(), kWidth, kHeight) && tiles.changed().empty(),
          "and is the picture kept");

    std::vector<std::uint8_t> smaller(static_cast<std::size_t>(100) * 100 * 4, 0);
    check(!tiles.weigh(smaller.data(), 100, 100), "a picture of a new size is sent whole");

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("changed tiles: all checks passed\n");
    return EXIT_SUCCESS;
}
