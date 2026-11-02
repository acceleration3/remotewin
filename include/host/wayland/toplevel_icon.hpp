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

class ToplevelIcon {
public:
    ToplevelIcon(const Globals& globals, const WindowIcon& icon);
    ~ToplevelIcon();

    ToplevelIcon(const ToplevelIcon&) = delete;
    ToplevelIcon& operator=(const ToplevelIcon&) = delete;
    ToplevelIcon(ToplevelIcon&&) = delete;
    ToplevelIcon& operator=(ToplevelIcon&&) = delete;

    void put_on(ToplevelRole& toplevel) const;

private:
    std::unique_ptr<ShmBuffer> pixels_;
    xdg_toplevel_icon_v1* icon_ = nullptr;
};

}
