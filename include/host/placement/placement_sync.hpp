#pragma once

#include "host/placement/kwin_placement.hpp"
#include "host/placement/screen_mapping.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSizeF>

#include <cstdint>
#include <optional>

namespace rwin::host {

class GuestLink;
class MirrorWindow;
struct MirrorState;

// Keeps a toplevel mirror where its guest window is: a window the guest
// moves - SetWindowPos, a dialog centred on its owner, CW_USEDEFAULT - is
// put at the same place on this desktop, and a mirror moved here - dragged
// by the user, placed by the compositor - has its guest window moved
// under it, so the guest's own idea of where the window is (the screen
// edge a menu flips at, the monitor a dialog centres on) is what is on
// screen. Only where the compositor can be told and asked (KWinPlacement);
// elsewhere the compositor places mirrors and the guest is not told.
class PlacementSync : public QObject {
    Q_OBJECT
public:
    explicit PlacementSync(GuestLink& link, QObject* parent = nullptr);

    [[nodiscard]] bool active() const noexcept { return kwin_.active(); }

    // What the mirror of ``window`` is known to the compositor by: a
    // suffix of its title, or nothing where nothing is placed.
    [[nodiscard]] QString tag_for(std::uint64_t window) const;

    // The guest window's picture, in guest screen pixels, as the window
    // list said. ``placeable`` is false for a window minimised or
    // maximised in the guest, and for a mirror that is a popup: those are
    // placed by their state and their owner, not by this. A mirror whose
    // surface was made again is placed again: the compositor put the new
    // one where it chose.
    void follow_guest(MirrorWindow& mirror, QRect guest, bool placeable);
    // A mirror maximised for the guest is maximised on the guest's own
    // screen - the output it is, here - as Windows maximises a window on the
    // monitor it is on; KWin would maximise it on the one its user is on.
    // Once per surface.
    void keep_on_guest_screen(MirrorWindow& mirror);
    void forget(std::uint64_t window);

    // The record of ``window`` into ``state``.
    void describe(std::uint64_t window, MirrorState& state) const;

    // Where the guest's screen - this desktop's primary output - is, as
    // the compositor lays it out; nothing where it does not say.
    [[nodiscard]] std::optional<ScreenMapping> guest_screen() const;

private:
    struct Tracked {
        QPointer<MirrorWindow> mirror;
        QPoint guest;
        std::uint32_t surface = 0;
        bool placeable = false;
        bool pending = false;
        // A placement of this window has been done, or asked for again
        // once KWin had it.
        bool placed_once = false;
        std::optional<QPointF> asked;
        QSizeF asked_size;
        std::uint32_t requests = 0;
        qint64 round_trip_ms = -1;
        std::optional<QRectF> reported;
        std::uint32_t guest_moves = 0;
        std::optional<QPoint> moved_guest_to;
        // A guest move asked for and not yet seen in a window list: when it
        // is, it is this side's own move coming back.
        std::optional<QPoint> awaiting_guest;
        // Since this side's placement was done: KWin's own placement of a
        // new window - on the output its user is on - can be reported after
        // it, and is not the user moving the window.
        QElapsedTimer since_placed;
        // Put back once already for this placement: a second report is
        // somebody's own.
        bool put_back = false;
        // The surface last sent to the guest's output.
        std::uint32_t on_guest_screen = 0;
    };

    void placed(std::uint64_t window, QPointF frame_corner, qint64 round_trip_ms);
    void not_placed(std::uint64_t window);
    void moved(std::uint64_t window, QRectF frame);

    GuestLink& link_;
    KWinPlacement kwin_;
    QHash<quint64, Tracked> tracked_;
};

}  // namespace rwin::host
