#include "host/mirror/mirror_scope.hpp"

#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "proto/window_fields.hpp"

#include <algorithm>
#include <cctype>
#include <string>
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

// A pid is only ever a process by its creation time as well: Windows hands a
// pid out again once its process is gone.
bool MirrorScope::is_launched(const WindowFields::ProcessRef& process) const {
    if (!launch_.process_id.has_value() || process.pid != *launch_.process_id) return false;
    return !launch_.process_created.has_value() || process.created == *launch_.process_created;
}

bool MirrorScope::in_launched_tree(const WindowFields::AppId& app) const {
    if (!launch_.same_app_id || !launch_.process_id.has_value()) return false;
    if (std::any_of(app.tree.begin(), app.tree.end(),
                    [this](const WindowFields::ProcessRef& process) { return is_launched(process); }))
        return true;
    return tree_root_.has_value() && !app.tree.empty() && app.tree.back() == *tree_root_;
}

std::vector<std::string> MirrorScope::claim(const GuestWindows& windows) {
    std::vector<std::string> fresh;
    if (!launch_.same_app_id) return fresh;
    // The tree's root comes from a window of the launched process, or of one
    // it started: the pids up from either end there.
    for (const GuestWindow* window : windows.stacked()) {
        const WindowFields::AppId& app = window->fields().app_id;
        if (tree_root_.has_value() || !launch_.process_id.has_value()) break;
        const bool inside = std::any_of(app.tree.begin(), app.tree.end(),
                                        [this](const WindowFields::ProcessRef& process) { return is_launched(process); });
        if (inside) tree_root_ = app.tree.back();
    }
    if (tree_root_.has_value() && !tree_claimed_) {
        tree_claimed_ = true;
        fresh.push_back("tree:" + std::to_string(tree_root_->pid) + ":" + std::to_string(tree_root_->created));
    }
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
    // The window's own process is the tree's first; a window that came with
    // no tree (its process unreadable) is known by its pid alone.
    if (app.tree.empty() ? launch_.process_id.has_value() && app.process_id == *launch_.process_id
                         : is_launched(app.tree.front()))
        return true;
    if (in_launched_tree(app)) return true;
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
