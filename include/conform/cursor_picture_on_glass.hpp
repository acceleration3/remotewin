#pragma once

#include "host/mirror/mirror_state.hpp"
#include "proto/probe/cursor_truth.hpp"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>

#include <optional>

namespace rwin::conform {

// A cursor as the user sees it on one side's screen, in that screen's
// pixels: drawn over one flat colour, with the pixel the pointer is at.
//
// The guest's is what DrawIconEx drew (CursorTruth). The mirror's is the
// pixmap it handed Qt, at the size the compositor shows it - its cursor
// surface's logical size (a viewport where it set one, the pixmap's size
// over its ratio otherwise) times the screen's ratio - resampled only when
// that is not the pixmap's own size.
class CursorPictureOnGlass {
public:
    [[nodiscard]] static CursorPictureOnGlass of_guest(const CursorTruth::Frame& frame, bool over_white);
    // Nothing when the mirror wears no pixels of its own.
    [[nodiscard]] static std::optional<CursorPictureOnGlass> of_mirror(const host::MirrorState& mirror, bool over_white);

    [[nodiscard]] const QImage& image() const noexcept { return image_; }
    [[nodiscard]] QPoint hotspot() const noexcept { return hotspot_; }
    // Screen pixels per pixel of the picture it was made from: 1 is a
    // picture shown as drawn.
    [[nodiscard]] double scale() const noexcept { return scale_; }

    // Laid hotspot on hotspot with ``other``, each on a canvas spanning
    // both: this one's canvas.
    [[nodiscard]] QImage aligned_with(const CursorPictureOnGlass& other) const;
    // The pixels not of the background, relative to the hotspot: where the
    // cursor is drawn around the point it points with.
    [[nodiscard]] QRect drawn_around_hotspot() const;

private:
    CursorPictureOnGlass(QImage image, QPoint hotspot, QColor background, double scale);

    QImage image_;
    QPoint hotspot_;
    QColor background_;
    double scale_ = 1.0;
};

}  // namespace rwin::conform
