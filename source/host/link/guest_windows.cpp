#include "host/link/guest_windows.hpp"

#include "proto/min_max.hpp"
#include "proto/window_fields.hpp"

#include <algorithm>
#include <utility>

namespace rwin::host {

GuestWindows::Told GuestWindows::tell(WindowId id, WindowFields fields) {
    unnamed_.erase(id);
    const auto known = windows_.find(id);
    if (known == windows_.end()) {
        windows_.emplace(id, GuestWindow{id, std::move(fields)});
        return Told{false, WindowFields::kAll};
    }
    const std::uint32_t changed = fields.differences(known->second.fields());
    known->second.take(std::move(fields));
    return Told{true, changed};
}

bool GuestWindows::change(WindowId id, WindowFields fields) {
    const auto known = windows_.find(id);
    if (known == windows_.end()) return false;
    known->second.take(std::move(fields));
    return true;
}

bool GuestWindows::forget(WindowId id) {
    unnamed_.erase(id);
    return windows_.erase(id) != 0;
}

bool GuestWindows::limit(const MinMax& limits) {
    const auto known = windows_.find(limits.window);
    if (known == windows_.end()) return false;
    known->second.limit(limits);
    return true;
}

void GuestWindows::restack(ZOrder order) { order_ = std::move(order); }

void GuestWindows::begin_sync() {
    syncing_ = true;
    unnamed_.clear();
    for (const auto& [id, window] : windows_) unnamed_.insert(id);
}

std::vector<WindowId> GuestWindows::end_sync() {
    std::vector<WindowId> gone;
    if (!syncing_) return gone;
    syncing_ = false;
    gone.reserve(unnamed_.size());
    for (const WindowId id : unnamed_) {
        windows_.erase(id);
        gone.push_back(id);
    }
    unnamed_.clear();
    return gone;
}

const GuestWindow* GuestWindows::find(WindowId id) const {
    const auto known = windows_.find(id);
    return known == windows_.end() ? nullptr : &known->second;
}

const GuestWindow* GuestWindows::find_hwnd(std::uint32_t hwnd) const {
    const auto known = std::find_if(windows_.begin(), windows_.end(),
                                    [hwnd](const auto& entry) { return entry.first.hwnd == hwnd; });
    return known == windows_.end() ? nullptr : &known->second;
}

std::vector<const GuestWindow*> GuestWindows::stacked() const {
    std::vector<const GuestWindow*> stacked;
    stacked.reserve(windows_.size());
    for (const WindowId id : order_.windows) {
        if (const GuestWindow* window = find(id)) stacked.push_back(window);
    }
    for (const auto& [id, window] : windows_) {
        if (std::find(order_.windows.begin(), order_.windows.end(), id) == order_.windows.end()) {
            stacked.push_back(&window);
        }
    }
    return stacked;
}

}
