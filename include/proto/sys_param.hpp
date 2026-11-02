#pragma once

#include "proto/rect.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct SysParam {
    enum class Param : std::uint32_t { DragFullWindows = 0x0025, WorkArea = 0x002F, Monitor = 0xF001 };

    Param param = Param::WorkArea;
    Rect area;
    bool enabled = false;

    [[nodiscard]] bool operator==(const SysParam&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SysParam> read(MessageReader& in);
};

}
