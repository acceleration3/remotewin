#include "guest/windows/window_directory.hpp"

#include <cstdint>

namespace rwin::guest {

void WindowDirectory::admit(WindowId id) {
    const std::lock_guard held{lock_};
    windows_.insert_or_assign(id.hwnd, Entry{id.serial, std::nullopt});
}

void WindowDirectory::forget(WindowId id) {
    const std::lock_guard held{lock_};
    const auto found = windows_.find(id.hwnd);
    if (found != windows_.end() && found->second.serial == id.serial) windows_.erase(found);
}

void* WindowDirectory::handle_of(WindowId id) const {
    const std::lock_guard held{lock_};
    const auto found = windows_.find(id.hwnd);
    if (found == windows_.end() || found->second.serial != id.serial || id.serial == 0) return nullptr;
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(id.hwnd));
}

std::optional<WindowId> WindowDirectory::id_of(void* window) const {
    const auto hwnd = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(window));
    const std::lock_guard held{lock_};
    const auto found = windows_.find(hwnd);
    if (found == windows_.end()) return std::nullopt;
    return WindowId{hwnd, found->second.serial};
}

bool WindowDirectory::set_picture(WindowId id, std::optional<Rect> trimmed) {
    const std::lock_guard held{lock_};
    const auto found = windows_.find(id.hwnd);
    if (found == windows_.end() || found->second.serial != id.serial || found->second.picture == trimmed) return false;
    found->second.picture = trimmed;
    return true;
}

std::optional<Rect> WindowDirectory::picture_of(WindowId id) const {
    const std::lock_guard held{lock_};
    const auto found = windows_.find(id.hwnd);
    if (found == windows_.end() || found->second.serial != id.serial) return std::nullopt;
    return found->second.picture;
}

}  // namespace rwin::guest
