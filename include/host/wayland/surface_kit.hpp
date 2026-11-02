#pragma once

namespace rwin::host {

class DrmSyncDevice;
class EventLoop;
class Globals;
class Seat;

struct SurfaceKit {
    const Globals& globals;
    EventLoop& loop;
    Seat& seat;
    const DrmSyncDevice& sync;
};

}
