#include "host/mirror/popup_stack.hpp"

#include "host/mirror/window_mirror.hpp"

#include <algorithm>

namespace rwin::host {

namespace {

bool descends_from(const WindowMirror& mirror, const WindowMirror& ancestor) {
    for (const WindowMirror* up = mirror.parent(); up != nullptr; up = up->parent()) {
        if (up == &ancestor) return true;
    }
    return false;
}

}

void PopupStack::opened(WindowMirror& popup) { open_.push_back(&popup); }

void PopupStack::closing(WindowMirror& popup) {
    const auto found = std::find(open_.begin(), open_.end(), &popup);
    if (found == open_.end()) return;
    const auto at = static_cast<std::size_t>(found - open_.begin());
    close_from(at + 1);
    open_.erase(open_.begin() + static_cast<std::ptrdiff_t>(at));
}

void PopupStack::closing_under(const WindowMirror& mirror) {
    const auto first = std::find_if(open_.begin(), open_.end(),
                                    [&](const WindowMirror* popup) { return descends_from(*popup, mirror); });
    if (first != open_.end()) close_from(static_cast<std::size_t>(first - open_.begin()));
}

bool PopupStack::open_beside(const WindowMirror& parent) const {
    return std::any_of(open_.begin(), open_.end(), [&](const WindowMirror* popup) {
        return popup != &parent && !descends_from(parent, *popup);
    });
}

void PopupStack::close_from(std::size_t first) {
    while (open_.size() > first) {
        WindowMirror* top = open_.back();
        open_.pop_back();
        top->drop_popup();
    }
}

}
