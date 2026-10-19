#pragma once

#include "host/cursor/cursor_player.hpp"
#include "host/cursor/pixel_cursor.hpp"
#include "host/input/pointer_grabber.hpp"
#include "host/mirror/activation_sync.hpp"
#include "host/mirror/mirror_scope.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/placement/placement_sync.hpp"
#include "host/wayland/pointer_globals.hpp"
#include "proto/move_size_start.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QImage;
class QTimer;

namespace rwin::host {

class GuestLink;
class GuestWindow;
class GuestWindows;
class MirrorWindow;
class RelativePointer;
class SharedFrameFeed;
struct SharedFrame;

// The windows of one guest application, mirrored for as long as it has
// any (MirrorScope says which are its).
//
// One application's windows belong together, share a taskbar identity, and
// end together. When it has had none for a while there is nothing left to
// show and the session says so.
class MirrorSession : public QObject {
    Q_OBJECT
public:
    // ``frames`` hands on the frames that stay on the GPU, when it was
    // opened: the mirrors draw those as well as frames of pixels.
    MirrorSession(GuestLink& link, SharedFrameFeed& frames, MirrorScope scope, QObject* parent = nullptr);
    ~MirrorSession() override;

    MirrorSession(const MirrorSession&) = delete;
    MirrorSession& operator=(const MirrorSession&) = delete;
    MirrorSession(MirrorSession&&) = delete;
    MirrorSession& operator=(MirrorSession&&) = delete;

    // The mirror of a guest window, or null while there is none. For a
    // harness that inspects mirrors and puts input into them the way the
    // desktop would.
    [[nodiscard]] MirrorWindow* find(WindowId window) const;
    // The application's top-most window that can be active, made active
    // in the guest and here: the manager's RAISE.
    void raise();

    // What the mirror of ``window`` decided, and where it was put on the
    // desktop; nothing while there is no mirror.
    [[nodiscard]] std::optional<MirrorState> state_of(WindowId window) const;
    // Every window the guest has, this application's or not.
    [[nodiscard]] const GuestWindows& windows() const noexcept;
    [[nodiscard]] bool belongs(WindowId window) const;

    // Where the guest's screen is on this desktop, where that is known.
    [[nodiscard]] std::optional<ScreenMapping> guest_screen() const { return placement_.guest_screen(); }

    // Where the held pointer's motion goes: the relative pointer protocol
    // feeds it, and a harness can in its place.
    [[nodiscard]] PointerGrabber& pointer_grabber() noexcept { return grabber_; }

    // What the compositor was found to offer below Qt, for a report.
    [[nodiscard]] const QString& pointer_protocols() const noexcept { return pointer_globals_.found(); }

Q_SIGNALS:
    // The application has had no window for kEmptyGraceMs. It is done.
    void finished();
    void ready();
    // A window of the application's showed the AppUserModelID it is known
    // by, the first time it is seen.
    void app_claimed(const QString& aumid);

private:
    void connected();
    void sync_began();
    void sync_ended();
    void window_new();
    void window_changed(WindowId window, quint32 mask);
    void window_gone(WindowId window);
    void window_limited(WindowId window);
    void work_area_changed(QRect area);
    void restacked(const ZOrder& order);
    void move_size_started(const MoveSizeStart& start);

    // Mirrors for every window of the application's that is shown, none for
    // any other.
    void reconcile();
    void claim_applications();
    void open_mirrors(std::vector<const GuestWindow*> arrivals);
    void open(const GuestWindow& window);
    void watch(MirrorWindow& mirror);
    void follow(MirrorWindow& mirror, const GuestWindow& window, quint32 mask);
    void place(MirrorWindow& mirror, const GuestWindow& window);
    // The window whose mirror ``window``'s is a popup of, if it is one: its
    // owner, unless it stands alone, or the window it is placed against.
    [[nodiscard]] WindowId popup_parent_of(const GuestWindow& window) const;
    void place_popups_of(WindowId window);
    // ``window`` is gone, or no longer the application's.
    void close_mirror(WindowId window);
    // The windows a closing mirror's popups belong to are not closing with
    // it: Windows hides an owner without hiding what it owns.
    void rehome_popups_of(MirrorWindow& owner);
    void close_let_go();
    void bring_forward_for(WindowId window);
    [[nodiscard]] bool owned_by(const GuestWindow& window, WindowId owner) const;
    void count_windows(std::size_t belonging);
    void wear_cursor(const CursorLook* look, std::size_t step);
    void follow_grab(const PointerGrab& grab);
    void show_frame(WindowId window, const QImage& pixels);
    void show_shared_frame(WindowId window, const SharedFrame& frame);
    void note_ready();

    GuestLink& link_;
    SharedFrameFeed& frames_;
    MirrorScope scope_;
    // By WindowId::key.
    QHash<quint64, QPointer<MirrorWindow>> mirrors_;
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
    ActivationSync activation_;
    QSize work_area_;
    QTimer* empty_;
    // Between SyncBegin and SyncEnd the guest is saying what is already
    // there: mirrors are opened and closed once it has said all of it.
    bool syncing_ = false;
    bool had_windows_ = false;
    bool ready_ = false;
};

}  // namespace rwin::host
