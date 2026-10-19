#pragma once

#include "proto/rect.hpp"
#include "proto/window_id.hpp"

#include <mutex>
#include <optional>
#include <unordered_map>

namespace rwin::guest {

// Which HWND each WindowId the host was told about names, shared by every
// thread of the guest half: the window thread admits and forgets windows,
// input and capture look them up. An id whose serial is not the one the
// handle was admitted under names nothing, so a message about a closed
// window never reaches the window Windows gave its handle to next.
//
// It also keeps where each window's frames lie in its frame when the
// capture trims a margin the window does not draw: input is measured from
// there, and the window's GEOMETRY says so.
class WindowDirectory {
public:
    void admit(WindowId id);
    void forget(WindowId id);

    // Null when ``id`` names no window admitted now.
    [[nodiscard]] void* handle_of(WindowId id) const;
    [[nodiscard]] std::optional<WindowId> id_of(void* window) const;

    // Where the frames of ``id`` lie, relative to its frame; nothing while
    // they are the frame itself. Whether that is news.
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

}  // namespace rwin::guest
