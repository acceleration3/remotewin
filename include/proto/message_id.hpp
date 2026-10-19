#pragma once

#include <cstdint>

namespace rwin {

// Wire 6 (docs/rail-model.md §4). Host ids below 64, guest ids from 64, so
// a message decoded on the wrong side is obviously wrong rather than
// plausibly right. Each message belongs to one feature (Feature) and is
// sent only while that feature is active on the link.
//
// The conformance probe speaks the same framing on a connection of its
// own, in a range of its own: the harness's commands from 128, the
// probe's answers from 192. Nothing on the mirror's connection is ever in
// either, so a probe message reaching a mirror is refused as unknown
// rather than read as something it resembles.
enum class MessageId : std::uint8_t {
    // Host to guest.
    HostHello = 1,
    HostSysParam = 3,
    Activate = 4,
    SysCommand = 5,
    SysMenu = 6,
    WindowMove = 7,
    FrameRoute = 20,
    SlotFree = 21,
    Subscribe = 22,
    Unsubscribe = 23,
    ClockProbe = 24,
    WatchMonitor = 25,
    // One window's frames come as pixels from here on: the host cannot
    // draw that window's shared surfaces, whatever it does with the rest.
    WindowPixels = 27,
    PointerMove = 30,
    PointerButton = 31,
    PointerWheel = 32,
    PointerDelta = 33,
    PointerAt = 34,
    Key = 35,
    PointerLeave = 36,

    // Guest to host.
    GuestHello = 64,
    SyncBegin = 65,
    SyncEnd = 66,
    WindowNew = 68,
    WindowChanged = 69,
    WindowGone = 70,
    ZOrder = 73,
    MinMax = 74,
    MoveSizeStart = 75,
    MoveSizeEnd = 76,
    GuestSysParam = 81,
    SlotRing = 90,
    FrameInSlot = 91,
    Frame = 92,
    FramePatch = 93,
    SurfaceLost = 94,
    CursorShape = 95,
    CursorChanged = 96,
    PointerGrab = 97,
    ClockEcho = 98,

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

}  // namespace rwin
