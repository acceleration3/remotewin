#pragma once

#include "host/wayland/surface_kit.hpp"

#include <string>

namespace rwin::host {

class Activation;
class GuestLink;
class PlacementSync;
class PointerGrabber;
class PopupStack;
struct GuestDesktop;

struct MirrorKit {
    GuestLink& link;
    SurfaceKit surfaces;
    Activation& activation;
    PopupStack& popups;
    PointerGrabber& grabber;
    const GuestDesktop& desktop;
    const PlacementSync& placement;
    std::string app_id;
};

}
