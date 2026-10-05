#include "host/wayland/cursor_surface_viewport.hpp"

#include "host/wayland/pointer_globals.hpp"
#include "logging/logger.hpp"

#include <QtGui/private/qguiapplication_p.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandinputdevice_p.h>
#include <QtWaylandClient/private/qwaylandintegration_p.h>
#include <QtWaylandClient/private/qwaylandsurface_p.h>

#include <viewporter-client-protocol.h>
#include <wayland-client.h>

#include <cstring>

namespace rwin::host {

void CursorSurfaceViewport::Release::operator()(wp_viewport* viewport) const noexcept { wp_viewport_destroy(viewport); }

namespace {

// Qt's private classes have no stable layout: a build run against any Qt
// but the one it was compiled with does without (the cursor resampled).
bool same_qt_as_built() {
    if (std::strcmp(qVersion(), QT_VERSION_STR) == 0) return true;
    RWIN_INFO("cursor", "built against Qt {}, running on {}: pixel cursors are resampled", QT_VERSION_STR,
              qVersion());
    return false;
}

}  // namespace

CursorSurfaceViewport::CursorSurfaceViewport(const PointerGlobals& globals)
    : globals_(globals),
      usable_(globals.viewporter() != nullptr && globals.cursor_shapes() && same_qt_as_built()) {}

CursorSurfaceViewport::~CursorSurfaceViewport() = default;

// CursorSurface<Pointer> (qwaylandcursorsurface_p.h, which Qt does not
// install) derives from QWaylandSurface alone and first, so the pointer to
// one is the pointer to the other.
wl_surface* CursorSurfaceViewport::qt_cursor_surface() {
    auto* integration = dynamic_cast<QtWaylandClient::QWaylandIntegration*>(QGuiApplicationPrivate::platformIntegration());
    if (integration == nullptr || integration->display() == nullptr) return nullptr;
    QtWaylandClient::QWaylandInputDevice* device = integration->display()->defaultInputDevice();
    if (device == nullptr || device->pointer() == nullptr) return nullptr;
    auto* surface = reinterpret_cast<QtWaylandClient::QWaylandSurface*>(device->pointer()->getOrCreateCursorSurface());
    return surface == nullptr ? nullptr : surface->object();
}

bool CursorSurfaceViewport::size_next(QSize logical) {
    if (!usable_) return false;
    if (viewport_ == nullptr) {
        wl_surface* surface = qt_cursor_surface();
        if (surface == nullptr) {
            RWIN_WARN("cursor", "Qt has no cursor surface yet; the cursor is shown at a whole scale");
            return false;
        }
        viewport_.reset(wp_viewporter_get_viewport(globals_.viewporter(), surface));
    }
    if (size_ == logical) return true;
    size_ = logical;
    wp_viewport_set_destination(viewport_.get(), logical.width(), logical.height());
    return true;
}

}  // namespace rwin::host
