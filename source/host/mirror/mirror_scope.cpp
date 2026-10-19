#include "host/mirror/mirror_scope.hpp"

#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "proto/window_fields.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace rwin::host {

namespace {

// Windows compares paths without case and takes either slash.
std::string compared_path(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
        return c == '/' ? '\\' : static_cast<char>(std::tolower(c));
    });
    return path;
}

}  // namespace

MirrorScope::MirrorScope(Launch launch)
    : launch_(std::move(launch)), image_(compared_path(launch_.image)), claimed_(launch_.aumids) {}

bool MirrorScope::everything() const noexcept {
    return !launch_.process_id.has_value() && launch_.image.empty() && launch_.aumids.empty();
}

std::vector<std::string> MirrorScope::claim(const GuestWindows& windows) {
    std::vector<std::string> fresh;
    if (!launch_.same_app_id) return fresh;
    for (const GuestWindow* window : windows.stacked()) {
        const std::string& aumid = window->fields().app_id.aumid;
        if (aumid.empty() || !launched(*window)) continue;
        if (std::find(claimed_.begin(), claimed_.end(), aumid) != claimed_.end()) continue;
        claimed_.push_back(aumid);
        fresh.push_back(aumid);
    }
    return fresh;
}

bool MirrorScope::launched(const GuestWindow& window) const {
    const WindowFields::AppId& app = window.fields().app_id;
    if (launch_.process_id.has_value() && app.process_id == *launch_.process_id) return true;
    return !image_.empty() && compared_path(app.image) == image_;
}

bool MirrorScope::belongs_itself(const GuestWindow& window) const {
    if (launched(window)) return true;
    const std::string& aumid = window.fields().app_id.aumid;
    return !aumid.empty() && std::find(claimed_.begin(), claimed_.end(), aumid) != claimed_.end();
}

// Up through owners and anchors both - a menu of a dialog of the
// application's window - each window looked at once, so a chain that loops,
// which Windows does not make but a wire can, ends.
bool MirrorScope::belongs(WindowId window, const GuestWindows& windows) const {
    if (everything()) return true;
    std::vector<WindowId> seen;
    std::vector<WindowId> next{window};
    while (!next.empty()) {
        const WindowId id = next.back();
        next.pop_back();
        if (id.none() || std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back(id);
        const GuestWindow* known = windows.find(id);
        if (known == nullptr) continue;
        if (belongs_itself(*known)) return true;
        next.push_back(known->owner());
        next.push_back(known->anchor());
    }
    return false;
}

}  // namespace rwin::host
