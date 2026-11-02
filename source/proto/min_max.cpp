#include "proto/min_max.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void MinMax::write(MessageWriter& out) const {
    window.write(out);
    for (const std::int32_t value : {max_width, max_height, max_x, max_y, min_track_width, min_track_height,
                                     max_track_width, max_track_height}) {
        out.put_i32(value);
    }
}

std::optional<MinMax> MinMax::read(MessageReader& in) {
    MinMax info;
    const std::optional<WindowId> window = WindowId::read(in);
    if (!window.has_value()) return std::nullopt;
    info.window = *window;
    for (std::int32_t* value : {&info.max_width, &info.max_height, &info.max_x, &info.max_y, &info.min_track_width,
                                &info.min_track_height, &info.max_track_width, &info.max_track_height}) {
        *value = in.i32();
    }
    if (!in.ok()) return std::nullopt;
    return info;
}

}
