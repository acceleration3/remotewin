#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/size.hpp"
#include "host/mirror/mirror_role.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <string>

namespace rwin::host {
struct MirrorState {
    WindowId window;
    MirrorRole role;
    bool shown = false;
    bool mapped = false;
    bool on_gpu = false;
    bool activated = false;
    bool maximized = false;
    bool fullscreen = false;
    bool asked_minimized = false;
    bool asked_maximized = false;
    bool asked_fullscreen = false;
    bool modal = false;
    bool grabs = false;
    Box placed;
    std::string title;
    Size logical;
    double scale = 1.0;
    Size picture;
    std::uint64_t frames = 0;
    std::uint32_t generation = 0;
};

}
