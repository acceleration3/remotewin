#pragma once

namespace rwin::host {

class DrmSyncDevice;
class EventLoop;
class Globals;
class Seat;

// What every surface of a mirror is made with: the compositor's globals,
// the loop it waits on, the seat it hears input from, and the device
// explicit sync is done on.
struct SurfaceKit {
    const Globals& globals;
    EventLoop& loop;
    Seat& seat;
    const DrmSyncDevice& sync;
};

}  // namespace rwin::host
