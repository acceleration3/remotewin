#include "host/mirror/mirror_scope.hpp"

#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "host/mirror/guest_application.hpp"
#include "proto/window_fields.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace rwin::host {

MirrorScope::MirrorScope(Launch launch)
    : launch_(std::move(launch)), image_(GuestApplication::folded(launch_.image)), claimed_(launch_.aumids) {
    for (const std::string& aumid : launch_.aumids) identities_.push_back(GuestApplication::folded(aumid));
}

bool MirrorScope::everything() const noexcept {
    return !launch_.process_id.has_value() && launch_.image.empty() && launch_.aumids.empty();
}

bool MirrorScope::is_launched(const WindowFields::ProcessRef& process) const {
    if (!launch_.process_id.has_value() || process.pid != *launch_.process_id) return false;
    return !launch_.process_created.has_value() || process.created == *launch_.process_created;
}

bool MirrorScope::holds_launched(const std::vector<WindowFields::ProcessRef>& tree) const {
    if (!launch_.process_id.has_value()) return false;
    for (std::size_t at = 0; at < tree.size(); ++at) {
        if (is_launched(tree[at])) return true;
        const bool unknown = tree[at].created == 0 && at > 0 && tree[at].pid == *launch_.process_id;
        if (unknown && (!launch_.process_created.has_value() || tree[at - 1].created >= *launch_.process_created))
            return true;
    }
    return false;
}

bool MirrorScope::in_launched_tree(const WindowFields::AppId& app) const {
    if (!launch_.same_app_id || !launch_.process_id.has_value()) return false;
    if (holds_launched(app.tree)) return true;
    return tree_root_.has_value() && !app.tree.empty() && app.tree.back() == *tree_root_;
}

bool MirrorScope::own_process(const WindowFields::AppId& app) const {
    return app.tree.empty() ? launch_.process_id.has_value() && app.process_id == *launch_.process_id
                            : is_launched(app.tree.front());
}

bool MirrorScope::names_launch(const WindowFields::AppId& app) const {
    return own_process(app) || (!image_.empty() && GuestApplication::folded(app.image) == image_);
}

bool MirrorScope::launched_application(const WindowFields::AppId& app) const {
    if (everything() || names_launch(app)) return true;
    const GuestApplication application{app};
    return application.known()
           && std::find(identities_.begin(), identities_.end(), application.identity()) != identities_.end();
}

void MirrorScope::identify(const GuestWindows& windows) {
    const std::vector<const GuestWindow*> stacked = windows.stacked();
    for (const GuestWindow* window : stacked) {
        if (names_launch(window->fields().app_id)) remember(window->fields().app_id);
    }
    if (!identities_.empty() || !launch_.aumids.empty()) return;
    const auto first = std::find_if(stacked.begin(), stacked.end(), [this](const GuestWindow* window) {
        return !window->hidden() && launched(*window) && GuestApplication{window->fields().app_id}.known();
    });
    if (first != stacked.end()) remember((*first)->fields().app_id);
}

void MirrorScope::remember(const WindowFields::AppId& app) {
    const GuestApplication application{app};
    if (!application.known()) return;
    if (std::find(identities_.begin(), identities_.end(), application.identity()) != identities_.end()) return;
    identities_.push_back(application.identity());
}

std::vector<std::string> MirrorScope::claim(const GuestWindows& windows) {
    std::vector<std::string> fresh;
    identify(windows);
    if (!launch_.same_app_id) return fresh;
    for (const GuestWindow* window : windows.stacked()) {
        const WindowFields::AppId& app = window->fields().app_id;
        if (tree_root_.has_value() || !launch_.process_id.has_value()) break;
        if (holds_launched(app.tree)) tree_root_ = app.tree.back();
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
    return names_launch(app) || in_launched_tree(app);
}

bool MirrorScope::belongs_itself(const GuestWindow& window) const {
    if (launched(window)) return true;
    const std::string& aumid = window.fields().app_id.aumid;
    return !aumid.empty() && std::find(claimed_.begin(), claimed_.end(), aumid) != claimed_.end();
}

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

}
