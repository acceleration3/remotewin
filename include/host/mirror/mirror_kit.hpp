#pragma once

#include "host/wayland/surface_kit.hpp"

namespace rwin::host {

class Activation;
class GuestLink;
class IconBadge;
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
    const IconBadge& badge;
};

}
