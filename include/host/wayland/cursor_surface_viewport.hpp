#pragma once

#include <QSize>

#include <memory>
#include <optional>

struct wp_viewport;
struct wl_surface;

namespace rwin::host {

class PointerGlobals;

// The size Qt's cursor surface is shown at, apart from its buffer's
// (wp_viewport on it): what lets a cursor be one buffer pixel to one
// screen pixel at a fractional scale. Qt gives a pixel cursor's surface a
// whole buffer scale only (6.11: the ratio truncated), so at 1.2 a cursor
// drawn in screen pixels is stretched 1.2 times by the compositor; with a
// viewport its surface is sized so that it is not stretched at all.
//
// The surface is Qt's own, reached through its private API: it is the one
// Qt hands the compositor as the cursor, and Qt keeps it the cursor's.
// Usable only where the compositor draws named cursors itself
// (wp_cursor_shape_v1) - Qt then puts nothing but pixel cursors on that
// surface, all sized by whoever set the viewport - where it has a
// viewporter, and on the Qt this was built against.
class CursorSurfaceViewport {
public:
    explicit CursorSurfaceViewport(const PointerGlobals& globals);
    ~CursorSurfaceViewport();

    CursorSurfaceViewport(const CursorSurfaceViewport&) = delete;
    CursorSurfaceViewport& operator=(const CursorSurfaceViewport&) = delete;
    CursorSurfaceViewport(CursorSurfaceViewport&&) = delete;
    CursorSurfaceViewport& operator=(CursorSurfaceViewport&&) = delete;

    [[nodiscard]] bool usable() const noexcept { return usable_; }

    // The next cursor Qt puts on the surface is shown ``logical`` units
    // large. Takes effect with Qt's commit of it, which setCursor makes at
    // once when the pointer is over the window, and as it enters one
    // otherwise. False when Qt has no cursor surface to size yet.
    [[nodiscard]] bool size_next(QSize logical);

    [[nodiscard]] std::optional<QSize> size() const noexcept { return size_; }

private:
    struct Release {
        void operator()(wp_viewport* viewport) const noexcept;
    };

    [[nodiscard]] static wl_surface* qt_cursor_surface();

    const PointerGlobals& globals_;
    std::unique_ptr<wp_viewport, Release> viewport_;
    std::optional<QSize> size_;
    bool usable_ = false;
};

}  // namespace rwin::host
