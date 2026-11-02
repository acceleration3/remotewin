#include "host/cursor/unit_padding.hpp"

#include <cmath>
#include <cstdlib>

namespace rwin::host {

namespace {

constexpr int kLargestDenominator = 120;
constexpr int kLargestNumerator = 48;
constexpr double kExact = 1e-3;

int round_up(int value, int multiple) { return ((value + multiple - 1) / multiple) * multiple; }

}

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

}
