#include "host/mirror/host_app_ids.hpp"

#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "host/mirror/guest_application.hpp"
#include "host/mirror/mirror_scope.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace rwin::host {

HostAppIds::HostAppIds(std::string launched, const MirrorScope& scope) : launched_(std::move(launched)), scope_(scope) {}

std::string HostAppIds::of(WindowId window, const GuestWindows& windows) const {
    const GuestWindow* owning = windows.find(window);
    if (owning == nullptr) return launched_;
    for (std::size_t steps = 0; steps < windows.size(); ++steps) {
        const GuestWindow* owner = windows.find(owning->owner());
        if (owner == nullptr) break;
        owning = owner;
    }
    return of_application(*owning);
}

std::string HostAppIds::of_application(const GuestWindow& window) const {
    const GuestApplication application{window.fields().app_id};
    if (launched_.empty()) return application.host_app_id();
    if (!application.known() || scope_.launched_application(window.fields().app_id)) return launched_;
    return application.host_app_id();
}

}
