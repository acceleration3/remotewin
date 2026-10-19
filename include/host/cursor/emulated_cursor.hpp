#pragma once

#include "host/cursor/cursor_look.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace rwin {
struct CursorPicture;
}

namespace rwin::host {

// The guest's cursors, as they have been sent: once each, then named.
//
// A pointer moving between a text field and a button does not resend two
// bitmaps thirty times a second. Kept at a stable place each, so a look
// handed out stays valid until it is let go of - and only the cursor shown
// longest ago is let go of, never the one being worn.
class EmulatedCursor {
public:
    // The guest keeps the pictures it has sent to 64, letting go of the one
    // shown longest ago, and sends one again once it has let go of it. Kept
    // larger here in the same order, every picture the guest believes sent
    // is one this side still has.
    static constexpr std::size_t kCapacity = 128;

    void remember(const CursorPicture& picture);

    // The cursor, now the one shown most lately; null when it has not
    // arrived, which happens only if the guest names one before sending it.
    [[nodiscard]] const CursorLook* show(std::uint64_t id);
    [[nodiscard]] const CursorLook* find(std::uint64_t id) const;
    [[nodiscard]] std::size_t size() const noexcept { return looks_.size(); }

private:
    struct Kept {
        CursorLook look;
        std::uint64_t shown = 0;
    };

    void make_room();

    std::unordered_map<std::uint64_t, Kept> looks_;
    std::uint64_t shows_ = 0;
    std::uint64_t worn_ = 0;
};

}  // namespace rwin::host
