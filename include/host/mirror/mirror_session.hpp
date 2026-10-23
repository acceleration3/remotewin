#pragma once

#include "host/cursor/cursor_player.hpp"
#include "host/input/pointer_grabber.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "host/mirror/activation_sync.hpp"
#include "host/mirror/guest_desktop.hpp"
#include "host/mirror/mirror_kit.hpp"
#include "host/mirror/mirror_scope.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/mirror/popup_stack.hpp"
#include "host/mirror/window_mirror.hpp"
#include "host/placement/placement_sync.hpp"
#include "host/wayland/activation.hpp"
#include "host/wayland/relative_pointer.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/window_icon.hpp"
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rwin::host {

class BusConnection;
class GuestLink;
class GuestWindow;
class GuestWindows;
class SharedFrameFeed;

// The windows of one guest application, mirrored for as long as it has any
// (MirrorScope says which are its).
//
// One application's windows belong together, share a taskbar identity, and
// end together. When it has had none for a while there is nothing left to
// show and the session says so.
class MirrorSession {
public:
    struct Parts {
        EventLoop& loop;
        GuestLink& link;
        SharedFrameFeed& frames;
        BusConnection& bus;
        SurfaceKit surfaces;
        // The host output that stands for the guest's monitor, by name.
        std::string output;
    };

    // ``app_id`` is the identity the windows present as; ``token`` an
    // activation token the launcher handed over, for the first window.
    MirrorSession(const Parts& parts, MirrorScope scope, std::string app_id, std::string token);
    ~MirrorSession();

    MirrorSession(const MirrorSession&) = delete;
    MirrorSession& operator=(const MirrorSession&) = delete;
    MirrorSession(MirrorSession&&) = delete;
    MirrorSession& operator=(MirrorSession&&) = delete;

    // The application's top-most window that can be active, made active
    // in the guest and here: the manager's RAISE, with the token it was
    // handed, if any.
    void raise(const std::string& token);

    [[nodiscard]] std::optional<MirrorState> state_of(WindowId window) const;
    // Every window the guest has, this application's or not.
    [[nodiscard]] const GuestWindows& windows() const noexcept;
    [[nodiscard]] bool belongs(WindowId window) const;
    [[nodiscard]] const std::string& app_id() const noexcept { return kit_.app_id; }

    // The application has had no window for a while. It is done.
    Signal<> finished;
    Signal<> ready;
    // A window of the application's showed the AppUserModelID it is known
    // by, the first time it is seen.
    Signal<const std::string&> app_claimed;

private:
    void window_changed(WindowId window, std::uint32_t mask);
    void window_gone(WindowId window);
    void desktop_changed();
    void restacked(const ZOrder& order);

    // Mirrors for every window of the application's, shown where the guest
    // shows it, as what it is (MirrorRole); none for any other.
    void reconcile();
    void claim_applications();
    void arrange();
    void arrange_one(const GuestWindow& window, std::vector<WindowId>& arranging);
    [[nodiscard]] WindowMirror* find(WindowId window) const;
    WindowMirror& open(const GuestWindow& window);
    void close(WindowId window);
    void release_closed();
    void follow_placement(WindowMirror& mirror, const GuestWindow& window);
    void bring_forward_for(WindowId window);
    [[nodiscard]] bool owned_by(const GuestWindow& window, WindowId owner) const;
    void count_windows(std::size_t belonging);
    void follow_grab(const PointerGrab& grab);
    void follow_pointer();
    void note_ready();

    EventLoop& loop_;
    GuestLink& link_;
    SharedFrameFeed& frames_;
    MirrorScope scope_;
    GuestDesktop desktop_;
    PopupStack popups_;
    Activation activation_;
    PointerGrabber grabber_;
    PlacementSync placement_;
    MirrorKit kit_;
    CursorPlayer cursor_;
    ActivationSync focus_;
    std::unordered_map<WindowId, std::unique_ptr<WindowMirror>> mirrors_;
    // Gone in the guest, still here for a press they hold: the compositor
    // keeps the pointer with the surface pressed on until the release.
    std::vector<std::unique_ptr<WindowMirror>> releasing_;
    // The icon each window last had, for its mirror made later.
    std::unordered_map<WindowId, WindowIcon> icons_;
    // The pointer's own motion, for the mirror a button is held in; none
    // when the compositor has no relative pointer.
    std::unique_ptr<RelativePointer> held_motion_;
    PointerGrab grab_;
    std::string token_;
    std::unique_ptr<EventLoop::Timer> empty_;
    // Mirrors to arrange again once whatever is being done now is done: a
    // popup taken down for another's sake, a surface remade under its parts.
    std::unique_ptr<EventLoop::Timer> rearrange_;
    // Between SyncBegin and SyncEnd the guest is saying what is already
    // there: mirrors are opened and closed once it has said all of it.
    bool syncing_ = false;
    bool had_windows_ = false;
    bool ready_ = false;
};

}  // namespace rwin::host
