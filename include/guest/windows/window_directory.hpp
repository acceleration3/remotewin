#pragma once

#include "proto/rect.hpp"
#include "proto/window_id.hpp"

#include <mutex>
#include <optional>
#include <unordered_map>

namespace rwin::guest {
class WindowDirectory {
public:
    void admit(WindowId id);
    void forget(WindowId id);

    [[nodiscard]] void* handle_of(WindowId id) const;
    [[nodiscard]] std::optional<WindowId> id_of(void* window) const;

    bool set_picture(WindowId id, std::optional<Rect> trimmed);
    [[nodiscard]] std::optional<Rect> picture_of(WindowId id) const;

private:
    struct Entry {
        std::uint32_t serial = 0;
        std::optional<Rect> picture;
    };

    mutable std::mutex lock_;
    std::unordered_map<std::uint32_t, Entry> windows_;
};

}
