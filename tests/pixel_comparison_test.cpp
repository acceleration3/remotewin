#include "conform/pixel_comparison.hpp"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QRegion>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::conform::PixelComparison;

QImage flat(QColor colour) {
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(colour);
    return image;
}

const QRegion kWhole{QRect(0, 0, 40, 30)};

void identical_pictures_match() {
    const QImage picture = flat(QColor(10, 120, 200));
    const PixelComparison same{picture, picture, kWhole, 0};
    check(same.matches(), "a picture matches itself");
    check(same.compared() == 40 * 30, "every pixel in the region is compared");
    check(same.max_delta() == 0, "with no difference at all");
    check(same.bounds().isEmpty(), "and nowhere to point at");
}

void one_pixel_off_is_found_where_it_is() {
    const QImage guest = flat(QColor(10, 120, 200));
    QImage host = guest;
    host.setPixel(7, 5, qRgb(10, 170, 200));
    const PixelComparison one{guest, host, kWhole, 8};
    check(one.mismatched() == 1, "one differing pixel is one mismatch");
    check(one.max_delta() == 50, "its largest channel difference is reported");
    check(one.bounds() == QRect(7, 5, 1, 1), "and where it is");
    check(one.difference().pixel(7, 5) != one.difference().pixel(8, 5),
          "the difference picture marks it apart from its neighbours");
}

void the_tolerance_absorbs_rounding() {
    const QImage guest = flat(QColor(100, 100, 100));
    const QImage host = flat(QColor(102, 99, 100));
    check(PixelComparison(guest, host, kWhole, 2).matches(), "a blend rounded differently matches");
    check(!PixelComparison(guest, host, kWhole, 1).matches(), "but not past the tolerance");
}

void outside_the_region_is_not_counted() {
    const QImage guest = flat(Qt::black);
    QImage host = guest;
    host.setPixel(0, 0, qRgb(255, 255, 255));
    const PixelComparison inner{guest, host, QRegion(QRect(5, 5, 10, 10)), 0};
    check(inner.matches(), "a difference outside the region does not count");
    check(inner.compared() == 100, "only the region is compared");
}

void pictures_of_different_sizes_are_a_broken_precondition() {
    bool thrown = false;
    try {
        const PixelComparison mismatched{flat(Qt::black), QImage(10, 10, QImage::Format_RGB32), kWhole, 0};
        static_cast<void>(mismatched);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    check(thrown, "two sizes cannot be compared pixel for pixel");
}

}  // namespace

int main() {
    identical_pictures_match();
    one_pixel_off_is_found_where_it_is();
    the_tolerance_absorbs_rounding();
    outside_the_region_is_not_counted();
    pictures_of_different_sizes_are_a_broken_precondition();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("pixel comparison: all checks passed\n");
    return EXIT_SUCCESS;
}
