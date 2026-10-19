#pragma once

#include "host/wayland/shm_buffer.hpp"

#include <memory>

struct xdg_toplevel_icon_v1;

namespace rwin {
struct WindowIcon;
}

namespace rwin::host {

class Globals;
class ToplevelRole;

// A guest window's icon as its toplevel's (xdg_toplevel_icon_v1): the
// guest's pixels on a square of shared memory, which the icon needs kept
// for as long as it lives. Where the compositor has no such protocol, the
// app_id's own desktop entry - LSW's, made with the program's icon - is
// what the taskbar shows.
class ToplevelIcon {
public:
    ToplevelIcon(const Globals& globals, const WindowIcon& icon);
    ~ToplevelIcon();

    ToplevelIcon(const ToplevelIcon&) = delete;
    ToplevelIcon& operator=(const ToplevelIcon&) = delete;
    ToplevelIcon(ToplevelIcon&&) = delete;
    ToplevelIcon& operator=(ToplevelIcon&&) = delete;

    // Applied with the toplevel's next commit.
    void put_on(ToplevelRole& toplevel) const;

private:
    std::unique_ptr<ShmBuffer> pixels_;
    xdg_toplevel_icon_v1* icon_ = nullptr;
};

}  // namespace rwin::host
