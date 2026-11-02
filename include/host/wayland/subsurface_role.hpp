#pragma once

#include "host/geometry/point.hpp"

struct wl_subsurface;
struct wl_surface;

namespace rwin::host {

class Globals;
class MirrorSurface;

class SubsurfaceRole {
public:
    SubsurfaceRole(const Globals& globals, MirrorSurface& surface, wl_surface* parent);
    ~SubsurfaceRole();

    SubsurfaceRole(const SubsurfaceRole&) = delete;
    SubsurfaceRole& operator=(const SubsurfaceRole&) = delete;
    SubsurfaceRole(SubsurfaceRole&&) = delete;
    SubsurfaceRole& operator=(SubsurfaceRole&&) = delete;

    [[nodiscard]] wl_surface* parent() const noexcept { return parent_; }
    [[nodiscard]] bool place(Point at);
    void below_parent();

private:
    wl_subsurface* subsurface_;
    wl_surface* parent_;
    Point at_;
    bool placed_ = false;
};

}
