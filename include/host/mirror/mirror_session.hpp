#pragma once

#include "host/cursor/cursor_player.hpp"
#include "host/cursor/pixel_cursor.hpp"
#include "host/input/pointer_grabber.hpp"
#include "host/link/guest_window_info.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/placement/placement_sync.hpp"
#include "host/wayland/pointer_globals.hpp"
#include "proto/pointer_grab.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <cstdint>
#include <memory>
#include <optional>

class QTimer;

namespace rwin::host {

class GuestLink;
class MirrorWindow;
class RelativePointer;
class SharedFrameFeed;
struct SharedFrame;

// The windows of one guest application, mirrored for as long as it runs.
//
// Scoped to a process because that is the unit a user thinks in: one
// application's windows belong together, share a taskbar identity, and
// end together. When the last one closes there is nothing left to show
// and the session says so.
class MirrorSession : public QObject {
    Q_OBJECT
public:
    // ``frames`` hands on the frames that stay on the GPU, when it was
    // opened: the mirrors draw those as well as frames of pixels.
    MirrorSession(GuestLink& link, SharedFrameFeed& frames, std::uint32_t process_id,
                  QString job_name, QObject* parent = nullptr);
    ~MirrorSession() override;

    // The mirror of a guest window, or null while there is none. For a
    // harness that inspects mirrors and puts input into them the way the
    // desktop would.
    [[nodiscard]] MirrorWindow* find(std::uint64_t window) const;
    void raise();

    // What the mirror of ``window`` decided, and where it was put on the
    // desktop; nothing while there is no mirror.
    [[nodiscard]] std::optional<MirrorState> state_of(std::uint64_t window) const;
    // The guest's windows as they last arrived, topmost first.
    [[nodiscard]] const QList<GuestWindowInfo>& listed() const noexcept { return listed_; }

    // Where the guest's screen is on this desktop, where that is known.
    [[nodiscard]] std::optional<ScreenMapping> guest_screen() const {
        return placement_.guest_screen();
    }

    // Where the held pointer's motion goes: the relative pointer protocol
    // feeds it, and a harness can in its place.
    [[nodiscard]] PointerGrabber& pointer_grabber() noexcept { return grabber_; }

    // What the compositor was found to offer below Qt, for a report.
    [[nodiscard]] const QString& pointer_protocols() const noexcept {
        return pointer_globals_.found();
    }

Q_SIGNALS:
    // No windows left and no process to make more. The application is done.
    void finished();
    void ready();

private:
    using Listed = QHash<quint64, const GuestWindowInfo*>;

    void refresh(const QList<GuestWindowInfo>& windows, bool application_alive, QSize guest_room);
    void finish_if_done();
    void open_mirrors(QList<const GuestWindowInfo*> arrivals, const Listed& listed);
    void open(const GuestWindowInfo& window, MirrorWindow* owner);
    void follow(MirrorWindow& mirror, const GuestWindowInfo& window, const Listed& listed);
    void place(MirrorWindow& mirror, const GuestWindowInfo& window, const Listed& listed);
    void close_departed(const Listed& listed);
    // The windows a closing mirror's popups belong to are not closing with
    // it: Windows hides an owner without hiding what it owns.
    void rehome_popups_of(MirrorWindow& owner, const Listed& listed);
    void bring_forward_for(std::uint64_t window);
    void restore_guest_activation();
    [[nodiscard]] bool owned_by(const GuestWindowInfo& window, std::uint64_t owner) const;
    void close_let_go();
    void hand_over_drag(quint64 window);
    void wear_cursor(const CursorLook* look, std::size_t step);
    void follow_grab(const PointerGrab& grab);
    void show_frame(quint64 window, const QImage& pixels);
    void show_shared_frame(quint64 window, const SharedFrame& frame);
    void forget(quint64 window);

    GuestLink& link_;
    SharedFrameFeed& frames_;
    std::uint32_t process_id_;
    QString job_name_;
    QTimer* poll_;
    QHash<quint64, QPointer<MirrorWindow>> mirrors_;
    // The last window list, in the guest's z-order, topmost first.
    QList<GuestWindowInfo> listed_;
    // Gone in the guest, still here for a press they hold.
    QList<QPointer<MirrorWindow>> waiting_release_;
    // Bound before anything that holds the pointer or sizes its cursor.
    PointerGlobals pointer_globals_;
    PixelCursor pixel_cursor_{pointer_globals_};
    CursorPlayer cursor_;
    PointerGrabber grabber_;
    // The pointer's own motion, for the mirror a button is held in
    // (MirrorWindow::held_motion); none when the compositor has no
    // relative pointer.
    std::unique_ptr<RelativePointer> held_motion_;
    PointerGrab grab_;
    PlacementSync placement_;
    bool application_alive_ = true;
    bool ready_ = false;
};

}  // namespace rwin::host
