#include "host/mirror/icon_badge.hpp"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::host::IconBadge;

constexpr std::uint8_t kIconBlue = 40;
constexpr std::uint8_t kIconGreen = 80;
constexpr std::uint8_t kIconRed = 120;

class BadgeFiles {
public:
    BadgeFiles() : directory_(std::filesystem::temp_directory_path() / ("rwin-icon-badge-" + std::to_string(::getpid()))) {
        std::filesystem::create_directories(directory_);
    }
    ~BadgeFiles() { std::filesystem::remove_all(directory_); }
    BadgeFiles(const BadgeFiles&) = delete;
    BadgeFiles& operator=(const BadgeFiles&) = delete;

    [[nodiscard]] std::string written(const char* name, std::uint32_t width, std::uint32_t height,
                                      const std::vector<std::uint8_t>& pixels) const {
        const std::string path = (directory_ / name).string();
        std::ofstream file{path, std::ios::binary};
        for (const std::uint32_t side : {width, height}) {
            for (int byte = 0; byte < 4; ++byte) file.put(static_cast<char>(side >> (8 * byte) & 0xff));
        }
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        return path;
    }

    [[nodiscard]] std::string missing() const { return (directory_ / "absent").string(); }

private:
    std::filesystem::path directory_;
};

std::vector<std::uint8_t> solid(std::uint32_t width, std::uint32_t height, std::uint8_t blue, std::uint8_t green,
                                std::uint8_t red, std::uint8_t alpha) {
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t pixel = 0; pixel < width * height; ++pixel) {
        pixels.insert(pixels.end(), {blue, green, red, alpha});
    }
    return pixels;
}

std::vector<std::uint8_t> icon(int side) { return solid(side, side, kIconBlue, kIconGreen, kIconRed, 255); }

std::vector<std::uint8_t> stamped(const IconBadge& badge, int side) {
    std::vector<std::uint8_t> pixels = icon(side);
    badge.stamp(pixels, side, static_cast<std::size_t>(side) * 4);
    return pixels;
}

bool is(const std::vector<std::uint8_t>& pixels, int side, int x, int y, std::uint8_t blue, std::uint8_t green,
        std::uint8_t red, std::uint8_t alpha) {
    const std::uint8_t* at = pixels.data() + (static_cast<std::size_t>(y) * side + x) * 4;
    return at[0] == blue && at[1] == green && at[2] == red && at[3] == alpha;
}

void an_opaque_badge_covers_the_bottom_right_square_of_an_icon() {
    const BadgeFiles files;
    const IconBadge badge{files.written("red", 124, 124, solid(124, 124, 0, 0, 255, 255))};
    const struct {
        int icon;
        int badge;
        int inset;
    } sizes[] = {{256, 118, 4}, {48, 22, 1}, {32, 15, 1}, {16, 7, 0}};
    for (const auto& size : sizes) {
        check(IconBadge::side_on(size.icon) == size.badge, "the badge's side is 0.46 of the icon's");
        check(IconBadge::inset_on(size.icon) == size.inset, "the badge's inset is 4 in 256 of the icon's side");
        const std::vector<std::uint8_t> pixels = stamped(badge, size.icon);
        const int first = size.icon - size.badge - size.inset;
        const int end = size.icon - size.inset;
        bool inside = true;
        bool outside = true;
        for (int y = 0; y < size.icon; ++y) {
            for (int x = 0; x < size.icon; ++x) {
                if (x >= first && x < end && y >= first && y < end) {
                    inside = inside && is(pixels, size.icon, x, y, 0, 0, 255, 255);
                } else {
                    outside = outside && is(pixels, size.icon, x, y, kIconBlue, kIconGreen, kIconRed, 255);
                }
            }
        }
        check(inside, "every pixel of the badge's square is the badge's");
        check(outside, "every pixel outside the badge's square is the icon's, unchanged");
    }
}

void a_translucent_badge_is_laid_over_the_icon() {
    const BadgeFiles files;
    const IconBadge badge{files.written("half", 8, 8, solid(8, 8, 0, 0, 128, 128))};
    const std::vector<std::uint8_t> pixels = stamped(badge, 64);
    check(is(pixels, 64, 40, 40, 20, 40, 188, 255), "a half transparent badge leaves half of the icon under it");
    std::vector<std::uint8_t> clear = solid(64, 64, 0, 0, 0, 0);
    badge.stamp(clear, 64, 64 * 4);
    check(is(clear, 64, 40, 40, 0, 0, 128, 128), "over nothing the badge is itself");
    check(is(clear, 64, 10, 10, 0, 0, 0, 0), "and what it does not cover stays clear");
}

void a_badge_is_averaged_down_to_its_size() {
    const BadgeFiles files;
    std::vector<std::uint8_t> halves = solid(32, 32, 0, 0, 0, 255);
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; x += 2) halves[(y * 32 + x) * 4 + 1] = 200;
    }
    const IconBadge badge{files.written("stripes", 32, 32, halves)};
    std::vector<std::uint8_t> pixels = icon(35);
    badge.stamp(pixels, 35, 35 * 4);
    check(IconBadge::side_on(35) == 16, "a 35 pixel icon has a 16 pixel badge");
    bool averaged = true;
    for (int y = 18; y < 34; ++y) {
        for (int x = 18; x < 34; ++x) averaged = averaged && is(pixels, 35, x, y, 0, 100, 0, 255);
    }
    check(averaged, "each pixel of a badge halved in size is the mean of the four it stands for");
}

void an_icon_too_small_for_a_badge_is_left_alone() {
    const BadgeFiles files;
    const IconBadge badge{files.written("red", 124, 124, solid(124, 124, 0, 0, 255, 255))};
    for (const int side : {1, 8, 11}) {
        check(stamped(badge, side) == icon(side), "an icon whose badge would be under 6 pixels gets none");
    }
    check(stamped(badge, 13) != icon(13), "an icon whose badge is 6 pixels gets it");
}

void a_file_that_is_no_badge_leaves_icons_alone() {
    const BadgeFiles files;
    std::vector<std::uint8_t> shorter = solid(16, 16, 0, 0, 255, 255);
    shorter.pop_back();
    std::vector<std::uint8_t> longer = solid(16, 16, 0, 0, 255, 255);
    longer.push_back(0);
    const std::string header_only = files.missing() + "-header";
    std::ofstream{header_only, std::ios::binary}.write("\x10\0\0", 3);
    const IconBadge badges[] = {
        IconBadge{},
        IconBadge{files.missing()},
        IconBadge{header_only},
        IconBadge{files.written("short", 16, 16, shorter)},
        IconBadge{files.written("long", 16, 16, longer)},
        IconBadge{files.written("empty", 0, 0, {})},
        IconBadge{files.written("flat", 16, 0, {})},
        IconBadge{files.written("huge", 4096, 4096, solid(4, 4, 0, 0, 255, 255))},
    };
    for (const IconBadge& badge : badges) {
        check(stamped(badge, 64) == icon(64), "without a well formed badge an icon is unchanged");
    }
}

void a_buffer_too_small_for_its_icon_is_not_written() {
    const BadgeFiles files;
    const IconBadge badge{files.written("red", 16, 16, solid(16, 16, 0, 0, 255, 255))};
    std::vector<std::uint8_t> pixels = icon(32);
    const std::vector<std::uint8_t> before = pixels;
    badge.stamp(pixels, 64, 64 * 4);
    check(pixels == before, "an icon said to be larger than its pixels is left alone");
}

}

int main() {
    an_opaque_badge_covers_the_bottom_right_square_of_an_icon();
    a_translucent_badge_is_laid_over_the_icon();
    a_badge_is_averaged_down_to_its_size();
    an_icon_too_small_for_a_badge_is_left_alone();
    a_file_that_is_no_badge_leaves_icons_alone();
    a_buffer_too_small_for_its_icon_is_not_written();
    if (failures == 0) std::printf("icon badge: ok\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
