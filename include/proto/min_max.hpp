#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct MinMax {
    WindowId window;
    std::int32_t max_width = 0;
    std::int32_t max_height = 0;
    std::int32_t max_x = 0;
    std::int32_t max_y = 0;
    std::int32_t min_track_width = 0;
    std::int32_t min_track_height = 0;
    std::int32_t max_track_width = 0;
    std::int32_t max_track_height = 0;

    [[nodiscard]] bool operator==(const MinMax&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<MinMax> read(MessageReader& in);
};

}
