#include "host/mirror/draw_picture.hpp"
#include "host/mirror/pixel_mapping.hpp"

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what, double ratio, int value) {
    if (condition) return;
    std::printf("FAIL at ratio %.4g, %d: %s\n", ratio, value, what);
    ++failures;
}

using rwin::host::PixelMapping;

// 1.2 is this desktop's own; 1.25 and 2 are the other scales anyone runs;
// 1.75 is the one where a mirror's rounding slack reaches two pixels.
constexpr std::array kRatios{1.0, 1.2, 1.25, 1.5, 1.75, 2.0};
constexpr int kLargest = 4000;

void a_guest_size_is_shown_whole_and_no_larger_than_it_must_be() {
    for (const double ratio : kRatios) {
        const PixelMapping mapping{ratio};
        for (int pixels = 1; pixels <= kLargest; ++pixels) {
            const QSize logical = mapping.logical_size(QSize(pixels, pixels));
            check(logical.width() * ratio >= pixels - 1e-9, "the mirror holds every pixel",
                  ratio, pixels);
            check((logical.width() - 1) * ratio < pixels, "and not a unit more", ratio,
                  pixels);
            const QSize device = mapping.device_size(logical);
            check(device.width() >= pixels, "Qt's buffer holds the whole picture", ratio,
                  pixels);
        }
    }
}

// A mirror the user sized asks for what it holds; the answer must size it
// back to the same units, or every answer asks again.
void a_size_asked_for_comes_back_as_the_same_size() {
    for (const double ratio : kRatios) {
        const PixelMapping mapping{ratio};
        for (int units = 1; units <= kLargest; ++units) {
            const QSize asked = mapping.guest_size(QSize(units, units));
            check(mapping.logical_size(asked) == QSize(units, units),
                  "the guest size a mirror holds rounds back to the mirror", ratio, units);
            check(asked.width() <= units * ratio + 1e-9, "never more than the mirror holds",
                  ratio, units);
        }
        for (int pixels = 1; pixels <= kLargest; ++pixels) {
            const QSize logical = mapping.logical_size(QSize(pixels, pixels));
            check(mapping.logical_size(mapping.guest_size(logical)) == logical,
                  "a mirror sized for the guest asks for nothing that resizes it", ratio,
                  pixels);
        }
    }
}

void the_whole_numbers_a_ratio_lands_on_stay_whole() {
    const PixelMapping mapping{1.2};
    check(mapping.guest_size(QSize(5, 5)) == QSize(6, 6), "5 units at 1.2 hold 6 pixels",
          1.2, 5);
    check(mapping.logical_size(QSize(6, 6)) == QSize(5, 5), "6 pixels at 1.2 are 5 units",
          1.2, 6);
    check(mapping.logical_size(QSize(1000, 800)) == QSize(834, 667),
          "1000x800 at 1.2 is 834x667", 1.2, 1000);
    check(mapping.device_size(QSize(834, 667)) == QSize(1001, 800),
          "Qt backs 834x667 at 1.2 with 1001x800", 1.2, 834);
}

void every_device_pixel_is_one_guest_pixel() {
    for (const double ratio : kRatios) {
        for (const int origin : {0, 1, 7}) {
            const PixelMapping mapping{ratio, 1.0, QPoint(origin, origin)};
            for (int pixel = 0; pixel <= kLargest; ++pixel) {
                const QPoint guest(pixel, pixel);
                check(mapping.guest_point(mapping.logical_point(guest)) == guest,
                      "a guest pixel's corner maps back to it", ratio, pixel);
                const double centre = (origin + pixel + 0.5) / ratio;
                check(mapping.guest_point(QPointF(centre, centre)) == guest,
                      "the middle of a device pixel is its guest pixel", ratio, pixel);
            }
            check(mapping.guest_point(QPointF((origin - 0.5) / ratio, 0)).x() < 0,
                  "left of the picture is outside it", ratio, origin);
        }
    }
}

void a_guest_rectangle_is_covered_and_barely_more() {
    for (const double ratio : kRatios) {
        for (const double fit : {1.0, 0.75, 1.3}) {
            const PixelMapping mapping{ratio, fit, QPoint(3, 5)};
            for (int at = 0; at < 300; at += 7) {
                const QRect guest(at, at / 2, 17 + at, 9 + at / 3);
                const QRect shape = mapping.logical_rect(guest);
                const QPointF from = mapping.logical_point(guest.topLeft());
                const QPointF to = mapping.logical_point(
                    guest.topLeft() + QPoint(guest.width(), guest.height()));
                check(shape.left() <= from.x() + 1e-9 && shape.top() <= from.y() + 1e-9,
                      "the shape starts at or before the rectangle", ratio, at);
                check(shape.left() + shape.width() >= to.x() - 1e-9
                          && shape.top() + shape.height() >= to.y() - 1e-9,
                      "the shape ends at or after it", ratio, at);
                check(from.x() - shape.left() < 1 && shape.left() + shape.width() - to.x() < 1,
                      "and is less than a unit larger on each side", ratio, at);
            }
        }
    }
}

// A confinement: every unit of it is over guest pixels of the clip, and a
// unit more on any side would not be.
void a_clip_is_confined_to_the_units_wholly_inside_it() {
    for (const double ratio : kRatios) {
        for (const double fit : {1.0, 0.75, 1.3}) {
            const PixelMapping mapping{ratio, fit, QPoint(3, 5)};
            for (int at = 0; at < 300; at += 7) {
                const QRect clip(at, at / 2, 17 + at, 9 + at / 3);
                const QRect inside = mapping.logical_rect_inside(clip);
                const QPointF from = mapping.logical_point(clip.topLeft());
                const QPointF to = mapping.logical_point(clip.topLeft() + QPoint(clip.width(), clip.height()));
                check(!inside.isEmpty(), "a clip many units large confines to something", ratio, at);
                check(inside.left() >= from.x() - 1e-9 && inside.top() >= from.y() - 1e-9,
                      "the confinement starts at or after the clip", ratio, at);
                check(inside.left() + inside.width() <= to.x() + 1e-9
                          && inside.top() + inside.height() <= to.y() + 1e-9,
                      "and ends at or before it", ratio, at);
                check(inside.left() - from.x() < 1 && to.x() - (inside.left() + inside.width()) < 1
                          && inside.top() - from.y() < 1 && to.y() - (inside.top() + inside.height()) < 1,
                      "and is less than a unit smaller on each side", ratio, at);
                const QPoint first = mapping.guest_point(QPointF(inside.left() + 0.01, inside.top() + 0.01));
                const QPoint last = mapping.guest_point(
                    QPointF(inside.left() + inside.width() - 0.01, inside.top() + inside.height() - 0.01));
                check(clip.contains(first) && clip.contains(last), "its corners are over pixels of the clip", ratio,
                      at);
            }
        }
    }
    const PixelMapping six_fifths{1.2};
    check(six_fifths.logical_rect_inside(QRect(100, 60, 600, 360)) == QRect(84, 50, 499, 300),
          "a clip at 100,60 600x360 at 1.2 is units 84,50 499x300 (83.3 and 583.3 rounded in)", 1.2, 0);
    check(six_fifths.logical_rect_inside(QRect(10, 10, 1, 1)).isEmpty(),
          "a one-pixel clip holds no whole unit at 1.2", 1.2, 0);
}

void a_mirror_its_own_size_is_one_to_one_at_the_top_left() {
    for (const double ratio : kRatios) {
        const PixelMapping mapping{ratio};
        for (int pixels = 1; pixels <= kLargest; pixels += 3) {
            const QSize guest(pixels, pixels + 1);
            const QSize logical = mapping.logical_size(guest);
            check(mapping.fit_for(guest, logical) == 1.0, "a mirror sized for it is 1:1",
                  ratio, pixels);
            if (ratio >= 1.75) continue;
            check(mapping.centring(guest, logical).logical_point(QPoint(0, 0)) == QPointF(0, 0),
                  "its slack is left at the right and bottom", ratio, pixels);
        }
    }
}

void a_maximised_mirror_fits_the_guest_screen() {
    // 2560x1440 at 1.2, less a panel: 2133x1160 units, 2560x1392 pixels.
    const PixelMapping mapping{1.2};
    const QSize screen(2133, 1160);
    check(mapping.fit_for(QSize(2560, 1392), screen) == 1.0,
          "a guest screen the same as this one is shown 1:1", 1.2, 2560);
    check(mapping.fit_for(QSize(2560, 1300), screen) == 1.0,
          "one that matches across but is shorter is 1:1, with bars", 1.2, 1300);
    const double up = mapping.fit_for(QSize(1920, 1080), screen);
    check(std::abs(up - 1392.0 / 1080.0) < 1e-12, "a smaller one is scaled up to fit",
          1.2, 1920);
    const double down = mapping.fit_for(QSize(3840, 2160), screen);
    check(std::abs(down - 1392.0 / 2160.0) < 1e-12, "a larger one is scaled down to fit",
          1.2, 3840);
    const PixelMapping fitted = PixelMapping{1.2, up}.centring(QSize(1920, 1080), screen);
    const QRectF picture = fitted.picture(QSize(1920, 1080));
    check(picture.height() * 1.2 <= 1392 + 1e-9 && picture.width() * 1.2 <= 2560 + 1e-9,
          "and the fitted picture is inside the mirror", 1.2, 1920);
}

// The claim the whole mapping exists for: drawn through the same code a
// mirror paints with, onto a buffer at the screen's ratio, every pixel of a
// frame lands on exactly one device pixel and is that pixel unchanged.
// A checkerboard, because a doubled row or column and a resampled edge
// both show in it as a pixel of the wrong colour.
int painted_wrong(double ratio, QSize size, QPoint origin) {
    QImage frame(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(frame.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            row[x] = ((x + y) % 2 == 0) ? qRgba(255, 255, 255, 255)
                                        : qRgba(0, (x * 7) & 0xFF, (y * 13) & 0xFF, 255);
        }
    }
    const PixelMapping where{ratio, 1.0, origin};
    const QSize logical = where.logical_size(size) + QSize(2, 2);
    QImage target(where.device_size(logical), QImage::Format_ARGB32_Premultiplied);
    target.setDevicePixelRatio(ratio);
    target.fill(Qt::transparent);
    {
        QPainter painter(&target);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        rwin::host::draw_picture(painter, frame, where);
    }
    int wrong = 0;
    for (int y = 0; y < target.height(); ++y) {
        for (int x = 0; x < target.width(); ++x) {
            const QPoint from(x - origin.x(), y - origin.y());
            const QRgb expected = frame.rect().contains(from) ? frame.pixel(from) : 0;
            if (target.pixel(x, y) != expected) ++wrong;
        }
    }
    return wrong;
}

void a_frame_is_copied_not_resampled() {
    for (const double ratio : kRatios) {
        for (const QSize size : {QSize(1000, 800), QSize(1001, 613), QSize(77, 31)}) {
            for (const QPoint origin : {QPoint(0, 0), QPoint(1, 0), QPoint(3, 5)}) {
                const int wrong = painted_wrong(ratio, size, origin);
                check(wrong == 0, "every frame pixel on exactly one device pixel", ratio,
                      wrong);
            }
        }
    }
}

// A picture drawn on the GPU goes where QPainter puts one on the pixel
// path: the same device pixels, to the pixel, one to one or fitted.
void the_gpu_picture_lands_where_the_painted_one_does() {
    for (const double ratio : kRatios) {
        for (const QPoint origin : {QPoint(0, 0), QPoint(1, 0), QPoint(3, 5)}) {
            const PixelMapping where{ratio, 1.0, origin};
            const QSize size(1001, 613);
            check(where.device_picture(size) == QRect(origin, size),
                  "one to one, the GPU's picture is the frame's own size at the origin", ratio, origin.x());
            const QRectF painted = where.picture(size);
            const QRect device(static_cast<int>(std::lround(painted.x() * ratio)),
                               static_cast<int>(std::lround(painted.y() * ratio)),
                               static_cast<int>(std::lround(painted.width() * ratio)),
                               static_cast<int>(std::lround(painted.height() * ratio)));
            check(device == where.device_picture(size), "and the painter's rectangle is the same pixels", ratio,
                  origin.y());
        }
        const PixelMapping fitted = PixelMapping{ratio, 0.6}.centring(QSize(1920, 1080), QSize(1000, 700));
        const QRectF painted = fitted.picture(QSize(1920, 1080));
        const QRect gpu = fitted.device_picture(QSize(1920, 1080));
        check(std::abs(painted.x() * ratio - gpu.x()) < 1e-6 && std::abs(painted.width() * ratio - gpu.width()) < 1e-6,
              "fitted, the GPU's picture is the painter's too", ratio, gpu.width());
    }
}

}  // namespace

int main() {
    the_gpu_picture_lands_where_the_painted_one_does();
    a_guest_size_is_shown_whole_and_no_larger_than_it_must_be();
    a_size_asked_for_comes_back_as_the_same_size();
    the_whole_numbers_a_ratio_lands_on_stay_whole();
    every_device_pixel_is_one_guest_pixel();
    a_guest_rectangle_is_covered_and_barely_more();
    a_clip_is_confined_to_the_units_wholly_inside_it();
    a_mirror_its_own_size_is_one_to_one_at_the_top_left();
    a_maximised_mirror_fits_the_guest_screen();
    a_frame_is_copied_not_resampled();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
