#pragma once

#include "host/geometry/box.hpp"

namespace rwin::host {
class PopupEvents {
public:
    virtual void popup_placed(const Box& where) = 0;
    virtual void popup_dismissed() = 0;

protected:
    ~PopupEvents() = default;
};

}
