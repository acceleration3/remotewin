#pragma once

#include "host/cursor/cursor_look.hpp"
#include "host/wayland/cursor_surface_viewport.hpp"

#include <QCursor>

#include <optional>

class QWidget;

namespace rwin::host {

class PointerGlobals;

// Puts a guest cursor picture on a widget as its cursor, one guest pixel
// to one screen pixel at the widget's ratio.
//
// A guest pixel is a device pixel here, and a cursor surface is sized in
// logical units: at a ratio of p/q, a picture whose width and hotspot are
// multiples of p is exactly a whole number of logical units, so the picture
// is padded with transparency to that, its surface sized to it through a
// viewport (CursorSurfaceViewport), and its hotspot falls on a whole unit.
// Where there is no viewport to be had, the picture is resampled to the
// ratio rounded up, which Qt does take: the right size on the glass, a
// little soft.
class PixelCursor {
public:
    explicit PixelCursor(const PointerGlobals& globals);

    void put_on(QWidget& widget, const CursorLook::Frame& frame);

    // The size the cursor surface was last given, in logical units; nothing
    // where it is not sized (no viewport).
    [[nodiscard]] std::optional<QSize> surface_size() const noexcept { return viewport_.size(); }

private:
    [[nodiscard]] std::optional<QCursor> padded_for(const CursorLook::Frame& frame, double ratio);
    [[nodiscard]] static QCursor resampled_for(const CursorLook::Frame& frame, double ratio);

    CursorSurfaceViewport viewport_;
};

}  // namespace rwin::host
