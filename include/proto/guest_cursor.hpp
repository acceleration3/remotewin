#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The cursor the guest shows now: which picture (CursorPicture::id), or
// none at all. Hidden is a state of its own, said outright: ShowCursor(FALSE)
// and SetCursor(NULL) leave no picture to name, and a host that only heard
// about pictures went on showing the last one.
struct GuestCursor {
    std::uint64_t id = 0;
    bool shown = false;

    [[nodiscard]] bool operator==(const GuestCursor&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<GuestCursor> read(MessageReader& in);
};

}  // namespace rwin
