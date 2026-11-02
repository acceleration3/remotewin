#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct SurfaceLost {
    enum class Reason : std::uint8_t { CaptureFailed = 0 };

    WindowId window;
    Reason reason = Reason::CaptureFailed;

    [[nodiscard]] bool operator==(const SurfaceLost&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SurfaceLost> read(MessageReader& in);
};

}
