#pragma once

#include "proto/pointer_grab.hpp"

#include <Qt>

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::host {

// What a mirror decided to be, read off it rather than recomputed: the
// window kind and hints it asked the compositor for, the geometry it took,
// the shape and opacity it paints with, and the state it believes it is
// in. For checking a mirror against the window it mirrors from outside;
// nothing in the mirror reads this.
struct MirrorState {
    std::uint64_t handle = 0;
    // The guest window this one is a popup of, or 0 for a toplevel.
    std::uint64_t popup_of = 0;
    // The guest window whose mirror this toplevel is attached to - its
    // transient parent - or 0.
    std::uint64_t attached_to = 0;
    // Attached to a window the guest has disabled while this one is up:
    // the modal dialog of that window, which the mirror brings forward
    // when the window is clicked.
    bool modal = false;
    // Requests made of the compositor so far: to be the active window
    // (xdg-activation, which it may refuse), and to draw the user's
    // attention (QApplication::alert, urgency).
    std::uint32_t activation_requests = 0;
    std::uint32_t alerts = 0;
    Qt::WindowFlags flags;
    bool shows_without_activating = false;
    bool visible = false;
    bool minimized = false;
    bool maximized = false;
    bool active = false;
    // Whether input reaching the mirror is passed on to the guest.
    bool forwards_input = false;
    // Whether the user can size it: its minimum and maximum differ.
    bool resizable = false;
    QString title;
    // Logical units; global for a toplevel, and where it was put for a
    // popup.
    QPoint position;
    // Where the mirror asked to be put, in the same units: for a popup the
    // compositor's answer (position) is its own, snapped to a grid of its
    // choosing, and may be a unit away. A toplevel asks for nothing.
    QPoint placed;
    QSize logical_size;
    double device_pixel_ratio = 1.0;
    // Device pixels per guest pixel.
    double fit = 1.0;
    // The guest window's size as the mirror last heard it, in its pixels.
    QSize guest_size;
    // Where the guest's picture is drawn, in the mirror's device pixels.
    QRect picture;
    // The constant alpha the whole window is painted with.
    std::uint8_t opacity = 255;
    bool color_keyed = false;
    // A COLORREF, 0x00BBGGRR.
    std::uint32_t color_key = 0;
    // Logical units; empty when the mirror is unshaped.
    QRegion mask;
    // The cursor the mirror wears: none (Qt::BlankCursor), a shape this
    // desktop draws (cursor_shape), or the guest's pixels (Qt::BitmapCursor)
    // - the pixmap as Qt was handed it, at its ratio, with the hotspot in
    // the cursor surface's logical units, and the size that surface was
    // given (a viewport), where it was. The guest cursor it is (0: none yet,
    // or hidden), how many pictures it has and how long each lasts, and how
    // many steps of it have been shown.
    Qt::CursorShape cursor_shape = Qt::ArrowCursor;
    // The edges of the mirror's own resize band the pointer is over, which
    // stand for the window's sizing border: their cursor is the band's.
    Qt::Edges hovered_edges;
    std::uint64_t cursor_id = 0;
    QImage cursor_pixels;
    double cursor_pixmap_ratio = 1.0;
    QPoint cursor_hotspot;
    std::optional<QSize> cursor_surface_size;
    bool cursor_inverts = false;
    std::vector<int> cursor_delays_ms;
    std::uint64_t cursor_steps_taken = 0;
    // Frames shown so far. A count that has stopped moving is a mirror
    // that has caught up with its window.
    std::uint64_t frames_presented = 0;

    // A toplevel's place on the desktop, where the compositor can be told
    // and asked (PlacementSync): where this side last asked it to put the
    // frame's corner, in logical units, how often, and how long the last
    // took; where the compositor last said the frame is; and the moves of
    // the guest window asked for because of where it was put on this side.
    std::optional<QPointF> placement_asked;
    std::uint32_t placement_requests = 0;
    qint64 placement_round_trip_ms = -1;
    std::optional<QRectF> compositor_frame;
    std::uint32_t guest_moves = 0;
    std::optional<QPoint> guest_moved_to;

    // The guest wants this window's pointer held (PointerGrab), and what was
    // asked of the compositor for it: the kind, the region (logical units;
    // empty is the whole surface) or the place a lock leaves the pointer,
    // whether the compositor says it is in force, whether the user escaped
    // it, and the motion passed on as counts. ``unavailable`` says why
    // nothing could be asked.
    struct PointerHold {
        PointerGrab::Kind wanted = PointerGrab::Kind::None;
        PointerGrab::Kind asked = PointerGrab::Kind::None;
        QRegion region{};
        std::optional<QPointF> hint{};
        bool engaged = false;
        bool escaped = false;
        // Let go because the mirror lost the keyboard focus, and not held
        // again since.
        bool lost_focus = false;
        std::uint32_t requests = 0;
        std::uint32_t motions = 0;
        QPoint moved{};
        QString unavailable{};
    };
    std::optional<PointerHold> pointer_hold;
};

}  // namespace rwin::host
