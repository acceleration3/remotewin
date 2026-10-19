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

// What every window's mirror is made with: the link to the guest, what its
// surfaces are made with, and what the mirrors of one application share -
// activation, the popups shown, the pointer's hold, the guest's desktop,
// where the compositor is told windows go, and the identity they present.
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

}  // namespace rwin::host
