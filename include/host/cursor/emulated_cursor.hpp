#pragma once

#include "host/cursor/cursor_look.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace rwin {
struct CursorPicture;
}

namespace rwin::host {
class EmulatedCursor {
public:
    static constexpr std::size_t kCapacity = 128;

    void remember(const CursorPicture& picture);

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

}
