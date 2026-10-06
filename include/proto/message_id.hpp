#pragma once

#include <cstdint>

namespace rwin {

// Host ids below 64, guest ids from 64, so a message decoded on the wrong
// side is obviously wrong rather than plausibly right.
//
// The conformance probe speaks the same framing on a connection of its
// own, in a range of its own: the harness's commands from 128, the
// probe's answers from 192. Nothing on the mirror's connection is ever in
// either, so a probe message reaching a mirror is refused as unknown
// rather than read as something it resembles.
enum class MessageId : std::uint8_t {
    // Which application to mirror: a Job Object name, and the process
    // id that was launched as the fallback for when the job cannot be
    // opened. Neither means the whole desktop.
    WatchScope = 1,
    Subscribe = 2,
    Unsubscribe = 3,
    PointerMove = 4,
    PointerButton = 5,
    Key = 6,
    CloseWindow = 7,
    ResizeWindow = 8,
    FocusWindow = 9,
    // Where the user or the compositor put a window on this side: its
    // picture's corner, in the guest's screen pixels. The guest moves the
    // window there, so its own idea of where it is - which edge a menu
    // flips at, which monitor it is on - is what is on screen.
    MoveWindow = 10,
    // The pointer's own motion while it is locked to a window
    // (PointerDelta).
    PointerDelta = 11,
    // The host's WireHello: sent first, before anything else.
    HostHello = 12,
    // Mirror the guest's whole primary monitor as one picture rather than
    // an application's windows. Its frames, and the pointer and keys sent
    // to it, name kMonitorWindow.
    WatchMonitor = 13,
    // How the guest's frames are to come (FrameRoute): said once, after
    // the hello, and again if the host can no longer take them as it said.
    FrameRoute = 14,
    // The host has finished reading a slot of a window's ring (SlotFree):
    // the guest may copy into it again.
    SlotFree = 15,
    // A wheel turned over a mirror (PointerWheel).
    PointerWheel = 16,
    // The pointer at a point of the guest's screen while a button pressed
    // in a mirror is held (PointerAt).
    PointerAt = 17,

    WindowList = 64,
    Frame = 65,
    WindowGone = 66,
    // Windows started moving a window, and the guest stopped it. The
    // host owns the drag from here.
    DragStarted = 67,
    // A cursor's pictures (CursorPicture), once per cursor.
    CursorShape = 68,
    // The cursor the guest shows now, or that it shows none (GuestCursor).
    CursorChanged = 69,
    // What the application wants done with the pointer (PointerGrab), each
    // time that changes.
    PointerGrabbed = 70,
    // The guest's WireHello: sent first, before anything else.
    GuestHello = 71,
    // The parts of a window's picture that changed since its last frame:
    // [window:u64][picture width:u32][height:u32][count:u32] and count
    // times a PictureRect followed by its pixels. Applies only to the
    // picture of that size the host already has.
    FramePatch = 72,
    // A window's ring of shared surfaces (SlotRing), each time it is made:
    // its frames are copied into them on the guest's GPU from here on.
    SlotRing = 73,
    // A window's newest frame is in a slot of its ring (FrameInSlot).
    FrameInSlot = 74,

    ProbeCreateWindow = 128,
    ProbeObserve = 129,
    ProbeCaptureScreen = 130,
    ProbeDestroyWindow = 131,
    ProbeDestroyAll = 132,
    ProbeDrainInput = 133,
    ProbeSetBackdrop = 134,
    ProbeQuit = 135,
    ProbeAct = 136,
    ProbeObserveCursor = 137,

    ProbeHello = 192,
    ProbeWindowCreated = 193,
    ProbeTruth = 194,
    ProbeScreenPixels = 195,
    ProbeDone = 196,
    ProbeInput = 197,
    ProbeActed = 198,
    ProbeCursorTruth = 199,
};

// The window a monitor's frames and input name (WatchMonitor). No window
// has handle 0, so nothing meant for a window is read as meant for the
// monitor, or the other way round.
inline constexpr std::uint64_t kMonitorWindow = 0;

}  // namespace rwin
