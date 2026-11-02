#pragma once

#include "proto/window_id.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

struct ZOrder {
    static constexpr std::size_t kMaxWindows = 257;

    WindowId active;
    std::vector<WindowId> windows;

    [[nodiscard]] bool above_marker(WindowId window) const noexcept;

    [[nodiscard]] bool operator==(const ZOrder&) const = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ZOrder> read(MessageReader& in);
};

}
