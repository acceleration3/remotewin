#include "host/wayland/pointer_globals.hpp"

#include "host/wayland/owned_proxy.hpp"
#include "logging/logger.hpp"

#include <QGuiApplication>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>
#include <cstring>

namespace rwin::host {

namespace {

// The versions this client speaks; a compositor's newer ones are not asked
// for.
constexpr std::uint32_t kConstraintsVersion = 1;
constexpr std::uint32_t kRelativeVersion = 1;
constexpr std::uint32_t kViewporterVersion = 1;

QNativeInterface::QWaylandApplication* wayland() {
    auto* application = qGuiApp;
    return application == nullptr ? nullptr : application->nativeInterface<QNativeInterface::QWaylandApplication>();
}

void destroy_queue(wl_event_queue* queue) { wl_event_queue_destroy(queue); }
void destroy_wrapper(wl_display* wrapper) { wl_proxy_wrapper_destroy(wrapper); }

}  // namespace

struct PointerGlobals::Bound {
    OwnedProxy<zwp_pointer_constraints_v1, &zwp_pointer_constraints_v1_destroy> constraints;
    OwnedProxy<zwp_relative_pointer_manager_v1, &zwp_relative_pointer_manager_v1_destroy> relative;
    OwnedProxy<wp_viewporter, &wp_viewporter_destroy> viewporter;
    PointerGlobals* owner = nullptr;
};

// Bound on a queue of this object's own, so the round trip that collects
// the globals dispatches nothing of Qt's; each one bound is then moved to
// Qt's default queue, which Qt dispatches, so the objects made from it are
// heard from.
PointerGlobals::PointerGlobals() : bound_(std::make_unique<Bound>()) {
    bound_->owner = this;
    QNativeInterface::QWaylandApplication* native = wayland();
    if (native == nullptr || native->display() == nullptr) {
        found_ = QStringLiteral("not Wayland");
        RWIN_INFO("pointer", "not on Wayland: no pointer constraints, relative motion or cursor viewport");
        return;
    }
    wl_display* display = native->display();
    const OwnedProxy<wl_event_queue, &destroy_queue> queue{wl_display_create_queue(display)};
    const OwnedProxy<wl_display, &destroy_wrapper> wrapper{static_cast<wl_display*>(wl_proxy_create_wrapper(display))};
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper.get()), queue.get());
    const OwnedProxy<wl_registry, &wl_registry_destroy> registry{wl_display_get_registry(wrapper.get())};
    static constexpr wl_registry_listener kListener{&PointerGlobals::announce, &PointerGlobals::withdraw};
    wl_registry_add_listener(registry.get(), &kListener, bound_.get());
    if (wl_display_roundtrip_queue(display, queue.get()) < 0) {
        RWIN_WARN("pointer", "the round trip for the compositor's globals failed");
    }
    for (wl_proxy* proxy : {reinterpret_cast<wl_proxy*>(bound_->constraints.get()),
                            reinterpret_cast<wl_proxy*>(bound_->relative.get()),
                            reinterpret_cast<wl_proxy*>(bound_->viewporter.get())}) {
        if (proxy != nullptr) wl_proxy_set_queue(proxy, nullptr);
    }
    found_ = QStringLiteral("zwp_pointer_constraints_v1 %1, zwp_relative_pointer_manager_v1 %2, wp_viewporter %3, "
                            "wp_cursor_shape_manager_v1 %4")
                 .arg(bound_->constraints ? QStringLiteral("yes") : QStringLiteral("no"),
                      bound_->relative ? QStringLiteral("yes") : QStringLiteral("no"),
                      bound_->viewporter ? QStringLiteral("yes") : QStringLiteral("no"),
                      cursor_shapes_ ? QStringLiteral("yes") : QStringLiteral("no"));
    RWIN_INFO("pointer", "compositor offers {}", found_.toStdString());
}

PointerGlobals::~PointerGlobals() = default;

void PointerGlobals::announce(void* self, wl_registry* registry, std::uint32_t name, const char* interface,
                              std::uint32_t version) {
    auto* bound = static_cast<Bound*>(self);
    if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        bound->constraints.reset(static_cast<zwp_pointer_constraints_v1*>(wl_registry_bind(
            registry, name, &zwp_pointer_constraints_v1_interface, std::min(version, kConstraintsVersion))));
    } else if (std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        bound->relative.reset(static_cast<zwp_relative_pointer_manager_v1*>(wl_registry_bind(
            registry, name, &zwp_relative_pointer_manager_v1_interface, std::min(version, kRelativeVersion))));
    } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0) {
        bound->viewporter.reset(static_cast<wp_viewporter*>(
            wl_registry_bind(registry, name, &wp_viewporter_interface, std::min(version, kViewporterVersion))));
    } else if (std::strcmp(interface, "wp_cursor_shape_manager_v1") == 0) {
        bound->owner->cursor_shapes_ = true;
    }
}

void PointerGlobals::withdraw(void* /*self*/, wl_registry* /*registry*/, std::uint32_t /*name*/) {}

zwp_pointer_constraints_v1* PointerGlobals::constraints() const noexcept { return bound_->constraints.get(); }

zwp_relative_pointer_manager_v1* PointerGlobals::relative() const noexcept { return bound_->relative.get(); }

wp_viewporter* PointerGlobals::viewporter() const noexcept { return bound_->viewporter.get(); }

wl_compositor* PointerGlobals::compositor() {
    QNativeInterface::QWaylandApplication* native = wayland();
    return native == nullptr ? nullptr : native->compositor();
}

wl_pointer* PointerGlobals::pointer() {
    QNativeInterface::QWaylandApplication* native = wayland();
    return native == nullptr ? nullptr : native->pointer();
}

wl_surface* PointerGlobals::surface_of(QWindow* window) {
    if (window == nullptr || window->handle() == nullptr || wayland() == nullptr) return nullptr;
    QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
    if (native == nullptr) return nullptr;
    return static_cast<wl_surface*>(native->nativeResourceForWindow("surface", window));
}

}  // namespace rwin::host
