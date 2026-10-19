#include "host/cursor/unit_padding.hpp"

#include <cmath>
#include <cstdlib>

namespace rwin::host {

namespace {

constexpr int kLargestDenominator = 120;
constexpr int kLargestNumerator = 48;
constexpr double kExact = 1e-3;

int round_up(int value, int multiple) { return ((value + multiple - 1) / multiple) * multiple; }

}  // namespace

// The smallest q for which ratio * q is whole gives p = ratio * q: 1.2 is
// 6/5, 1.25 is 5/4, 2 is 2/1. Padded left and above so the hotspot lands on
// a multiple of p, and right and below to a multiple of p.
std::optional<UnitPadding> unit_padding(Size picture, Point hotspot, double ratio) {
    for (int q = 1; q <= kLargestDenominator; ++q) {
        const double whole = ratio * q;
        if (std::abs(whole - std::round(whole)) > kExact) continue;
        const int p = static_cast<int>(std::lround(whole));
        if (p < 1 || p > kLargestNumerator) return std::nullopt;
        const Point offset{(p - hotspot.x % p) % p, (p - hotspot.y % p) % p};
        const Size padded{round_up(offset.x + picture.width, p), round_up(offset.y + picture.height, p)};
        return UnitPadding{offset, padded, Size{padded.width / p * q, padded.height / p * q},
                           Point{(hotspot.x + offset.x) / p * q, (hotspot.y + offset.y) / p * q}};
    }
    return std::nullopt;
}

}  // namespace rwin::host
