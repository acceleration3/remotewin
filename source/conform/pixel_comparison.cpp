#include "conform/pixel_comparison.hpp"

#include <QColor>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace rwin::conform {

namespace {

constexpr int kMatchedShade = 3;   // a matching pixel's grey is its luma over this
constexpr int kIgnoredShade = 5;   // and an ignored pixel's, darker still
constexpr int kMismatchFloor = 128;

int delta(QRgb a, QRgb b) {
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                     std::abs(qBlue(a) - qBlue(b))});
}

}  // namespace

PixelComparison::PixelComparison(const QImage& guest, const QImage& host, const QRegion& region,
                                 int channel_tolerance) {
    if (guest.size() != host.size()) {
        throw std::invalid_argument("a pixel comparison needs two pictures of one size");
    }
    const QImage left = guest.convertToFormat(QImage::Format_RGB32);
    const QImage right = host.convertToFormat(QImage::Format_RGB32);
    difference_ = QImage(left.size(), QImage::Format_RGB32);
    for (int y = 0; y < left.height(); ++y) {
        const auto* from = reinterpret_cast<const QRgb*>(left.constScanLine(y));
        const auto* to = reinterpret_cast<const QRgb*>(right.constScanLine(y));
        auto* out = reinterpret_cast<QRgb*>(difference_.scanLine(y));
        for (int x = 0; x < left.width(); ++x) {
            const int grey = qGray(from[x]);
            if (!region.contains(QPoint(x, y))) {
                const int shade = grey / kIgnoredShade;
                out[x] = qRgb(shade, shade, shade + 24);
                continue;
            }
            ++compared_;
            const int d = delta(from[x], to[x]);
            max_delta_ = std::max(max_delta_, d);
            if (d <= channel_tolerance) {
                const int shade = grey / kMatchedShade;
                out[x] = qRgb(shade, shade, shade);
                continue;
            }
            ++mismatched_;
            bounds_ |= QRect(x, y, 1, 1);
            out[x] = qRgb(std::min(255, kMismatchFloor + d), 0, 0);
        }
    }
}

}  // namespace rwin::conform
