#include "host/mirror/icon_badge.hpp"

#include "logging/logger.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

namespace rwin::host {

namespace {
constexpr double kSideFraction = 0.46;
constexpr double kInsetFraction = 4.0 / 256.0;
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kBytesPerPixel = 4;

std::uint32_t little_endian(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8
           | static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
}

}

IconBadge::IconBadge(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        RWIN_WARN("icons", "the icon badge {} cannot be opened: icons get no badge", path);
        return;
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    if (bytes.size() < kHeaderBytes) {
        RWIN_WARN("icons", "the icon badge {} is {} bytes, shorter than its header: icons get no badge", path,
                  bytes.size());
        return;
    }
    const std::uint32_t width = little_endian(bytes.data());
    const std::uint32_t height = little_endian(bytes.data() + 4);
    if (width == 0 || height == 0 || width > kLargestPicture || height > kLargestPicture
        || bytes.size() != kHeaderBytes + static_cast<std::size_t>(width) * height * kBytesPerPixel) {
        RWIN_WARN("icons", "the icon badge {} says {}x{} and is {} bytes: icons get no badge", path, width, height,
                  bytes.size());
        return;
    }
    bytes.erase(bytes.begin(), bytes.begin() + kHeaderBytes);
    width_ = width;
    height_ = height;
    picture_ = std::move(bytes);
}

int IconBadge::side_on(int icon_side) noexcept { return static_cast<int>(std::lround(icon_side * kSideFraction)); }

int IconBadge::inset_on(int icon_side) noexcept { return static_cast<int>(std::lround(icon_side * kInsetFraction)); }

std::vector<std::uint8_t> IconBadge::scaled(int side) const {
    const auto target = static_cast<std::uint64_t>(side);
    std::vector<std::uint8_t> made(static_cast<std::size_t>(side) * side * kBytesPerPixel);
    const std::uint64_t whole = static_cast<std::uint64_t>(width_) * height_;
    for (std::uint64_t y = 0; y < target; ++y) {
        const std::uint64_t top = y * height_;
        const std::uint64_t bottom = top + height_;
        for (std::uint64_t x = 0; x < target; ++x) {
            const std::uint64_t left = x * width_;
            const std::uint64_t right = left + width_;
            std::array<std::uint64_t, kBytesPerPixel> sums{};
            for (std::uint64_t row = top / target; row * target < bottom; ++row) {
                const std::uint64_t tall = std::min(bottom, (row + 1) * target) - std::max(top, row * target);
                for (std::uint64_t column = left / target; column * target < right; ++column) {
                    const std::uint64_t wide = std::min(right, (column + 1) * target) - std::max(left, column * target);
                    const std::uint8_t* from = picture_.data() + (row * width_ + column) * kBytesPerPixel;
                    for (std::size_t channel = 0; channel < kBytesPerPixel; ++channel) {
                        sums[channel] += from[channel] * wide * tall;
                    }
                }
            }
            std::uint8_t* to = made.data() + (y * target + x) * kBytesPerPixel;
            for (std::size_t channel = 0; channel < kBytesPerPixel; ++channel) {
                to[channel] = static_cast<std::uint8_t>((sums[channel] + whole / 2) / whole);
            }
        }
    }
    return made;
}

void IconBadge::stamp(std::span<std::uint8_t> premultiplied_bgra, int side, std::size_t stride) const {
    if (picture_.empty() || side <= 0) return;
    const int badge = side_on(side);
    const int origin = side - badge - inset_on(side);
    if (badge < kSmallestSide || origin < 0) return;
    const std::size_t row_bytes = static_cast<std::size_t>(side) * kBytesPerPixel;
    if (stride < row_bytes || premultiplied_bgra.size() < stride * (static_cast<std::size_t>(side) - 1) + row_bytes) {
        return;
    }
    const std::vector<std::uint8_t> mark = scaled(badge);
    for (int row = 0; row < badge; ++row) {
        const std::uint8_t* from = mark.data() + static_cast<std::size_t>(row) * badge * kBytesPerPixel;
        std::uint8_t* to = premultiplied_bgra.data() + static_cast<std::size_t>(row + origin) * stride
                           + static_cast<std::size_t>(origin) * kBytesPerPixel;
        for (int column = 0; column < badge; ++column, from += kBytesPerPixel, to += kBytesPerPixel) {
            const unsigned kept = 255U - from[3];
            for (std::size_t channel = 0; channel < kBytesPerPixel; ++channel) {
                to[channel] = static_cast<std::uint8_t>(std::min(255U, from[channel] + (to[channel] * kept + 127) / 255));
            }
        }
    }
}

}
