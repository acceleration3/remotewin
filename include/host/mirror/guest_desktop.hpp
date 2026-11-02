#pragma once

#include "host/geometry/box.hpp"

namespace rwin::host {
struct GuestDesktop {
    Box monitor;
    Box work_area;
};

}
