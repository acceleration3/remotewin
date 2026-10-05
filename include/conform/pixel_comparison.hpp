#pragma once

#include <QImage>
#include <QRect>
#include <QRegion>

namespace rwin::conform {

// Two pictures of the same scene, pixel against pixel, over the part of it
// that is meant to agree.
//
// A pixel matches when no channel differs by more than the tolerance: the
// guest's compositor and the host's toolkit blend a translucent pixel with
// their own rounding, and a difference of one or two in a channel is that,
// not a different picture. Outside the region nothing is counted - the
// shadow around a window is drawn by each side's compositor and is looked
// at on its own - but it is still drawn, dimmed, in the difference image so
// a reader sees the scene the numbers are about.
class PixelComparison {
public:
    // ``guest`` and ``host`` are the same size; ``region`` is in their
    // pixels.
    PixelComparison(const QImage& guest, const QImage& host, const QRegion& region,
                    int channel_tolerance);

    [[nodiscard]] int compared() const noexcept { return compared_; }
    [[nodiscard]] int mismatched() const noexcept { return mismatched_; }
    // The largest difference in any channel of any compared pixel.
    [[nodiscard]] int max_delta() const noexcept { return max_delta_; }
    // Where the mismatched pixels are; empty when there are none.
    [[nodiscard]] QRect bounds() const noexcept { return bounds_; }
    [[nodiscard]] bool matches() const noexcept { return mismatched_ == 0; }
    // Mismatches in red, brighter for larger differences; matching pixels
    // in grey; what was not compared darkened.
    [[nodiscard]] const QImage& difference() const noexcept { return difference_; }

private:
    QImage difference_;
    QRect bounds_;
    int compared_ = 0;
    int mismatched_ = 0;
    int max_delta_ = 0;
};

}  // namespace rwin::conform
