#pragma once

#include "host/geometry/point.hpp"

struct wl_subsurface;
struct wl_surface;

namespace rwin::host {

class Globals;
class MirrorSurface;

// A surface as a part of another (wl_subsurface): drawn with its parent, at
// a place in its parent's logical units, above it, and moved with it. Its
// own frames go out as they come (desynchronised); its place is applied
// with its parent's next commit.
class SubsurfaceRole {
public:
    SubsurfaceRole(const Globals& globals, MirrorSurface& surface, wl_surface* parent);
    ~SubsurfaceRole();

    SubsurfaceRole(const SubsurfaceRole&) = delete;
    SubsurfaceRole& operator=(const SubsurfaceRole&) = delete;
    SubsurfaceRole(SubsurfaceRole&&) = delete;
    SubsurfaceRole& operator=(SubsurfaceRole&&) = delete;

    [[nodiscard]] wl_surface* parent() const noexcept { return parent_; }
    // False when it was where it is asked to be already.
    [[nodiscard]] bool place(Point at);
    void below_parent();

private:
    wl_subsurface* subsurface_;
    wl_surface* parent_;
    Point at_;
    bool placed_ = false;
};

}  // namespace rwin::host
