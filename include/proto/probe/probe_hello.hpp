#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct ProbeHello {
    std::uint32_t process_id = 0;
    std::uint32_t dpi = 0;
    std::uint32_t screen_width = 0;
    std::uint32_t screen_height = 0;
    std::uint32_t work_area_width = 0;
    std::uint32_t work_area_height = 0;
    bool drop_shadows = false;

    [[nodiscard]] bool operator==(const ProbeHello&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ProbeHello> read(MessageReader& in);
};

}
