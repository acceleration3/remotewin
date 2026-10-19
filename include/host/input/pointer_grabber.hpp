#pragma once

#include "host/mirror/mirror_state.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/window_id.hpp"

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRegion>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>

struct wl_pointer;
struct wl_surface;

namespace rwin::host {

class ConfinedPointer;
class GuestLink;
class LockedPointer;
class MirrorWindow;
class PointerGlobals;
class RelativePointer;

// Holds this desktop's pointer the way the guest application holds its
// own (PointerGrab): confined to the mirror's part that is the guest's
// clip rectangle, or locked on the mirror with only its motion passed on,
// as counts the guest moves its cursor by - which is what reaches a game's
// raw input.
//
// Asked is not held: the compositor holds the pointer when it chooses (the
// mirror has the focus and the pointer is over it) and says so. Until then
// the pointer moves freely over the mirror, as positions, and its motion is
// not passed on - a pointer crossing a mirror whose lock is pending does
// not turn the game's camera.
//
// Let go of when the guest stops asking, when the mirror loses the focus
// (and held again when it gets it back), and when the user presses the
// escape, Ctrl+Alt+G - QEMU's - after which a click on the mirror holds it
// again for as long as the guest still asks. While the compositor holds
// it, the guest is given motion only.
class PointerGrabber : public QObject {
    Q_OBJECT
public:
    PointerGrabber(GuestLink& link, const PointerGlobals& globals, QObject* parent = nullptr);
    ~PointerGrabber() override;

    // What the guest wants now; ``mirror`` is the mirror of the window it is
    // for, or null, and ``guest_origin`` where that window's picture is on
    // the guest's screen.
    void follow(const PointerGrab& grab, MirrorWindow* mirror, QPoint guest_origin);

    // The pointer's own motion, unaccelerated, in counts, passed on while
    // a lock is asked for: what the relative pointer protocol gives while
    // the compositor holds the pointer (motion_while_held), and what a
    // harness gives in its place - no client can make the compositor move a
    // pointer.
    void take_motion(QPointF unaccelerated);

    // What was asked of the compositor for ``window``'s mirror, if anything.
    void describe(WindowId window, MirrorState& state) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void hold();
    [[nodiscard]] QString why_unavailable(wl_surface* surface, wl_pointer* pointer);
    void let_go();
    // The relative pointer's motion: the compositor reports it whenever the
    // pointer moves over the client's surfaces, held or not.
    void motion_while_held(QPointF unaccelerated);
    [[nodiscard]] bool held() const noexcept;
    void watch(MirrorWindow* mirror);
    [[nodiscard]] bool wanted() const noexcept;
    [[nodiscard]] bool locking() const noexcept;
    [[nodiscard]] QRegion confine_region() const;
    [[nodiscard]] std::optional<QPointF> lock_hint() const;
    [[nodiscard]] bool escape_pressed(const QEvent* event) const;
    void notice_compositor_let_go();

    GuestLink& link_;
    const PointerGlobals& globals_;
    PointerGrab grab_;
    QPointer<MirrorWindow> mirror_;
    QPoint origin_;
    std::unique_ptr<LockedPointer> lock_;
    std::unique_ptr<ConfinedPointer> confinement_;
    std::unique_ptr<RelativePointer> relative_;
    bool escaped_ = false;
    bool lost_focus_ = false;
    bool swallow_escape_release_ = false;
    // Motion under a whole count, carried to the next.
    QPointF owed_;
    // What was asked, for a reader.
    QRegion asked_region_;
    std::optional<QPointF> asked_hint_;
    std::uint32_t requests_ = 0;
    std::uint32_t motions_ = 0;
    QPoint moved_;
    QString unavailable_;
};

}  // namespace rwin::host
