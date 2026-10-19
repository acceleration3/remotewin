#pragma once

#include "host/geometry/box.hpp"

namespace rwin::host {

// What a popup's owner hears from the compositor.
class PopupEvents {
public:
    // Where the compositor put it, relative to its parent's window
    // geometry; acknowledged already.
    virtual void popup_placed(const Box& where) = 0;
    // Dismissed (xdg_popup.popup_done): a press outside every surface of
    // this client's, an Escape the compositor took, its parent going. The
    // popup is unmapped and is to be destroyed.
    virtual void popup_dismissed() = 0;

protected:
    ~PopupEvents() = default;
};

}  // namespace rwin::host
