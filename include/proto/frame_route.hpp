#pragma once

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct FrameRoute {
    bool shared_surfaces = false;

    [[nodiscard]] bool operator==(const FrameRoute&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<FrameRoute> read(MessageReader& in);
};

}
