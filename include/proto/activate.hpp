#pragma once

#include "proto/window_id.hpp"

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct Activate {
    WindowId window;
    bool enabled = true;

    [[nodiscard]] bool operator==(const Activate&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<Activate> read(MessageReader& in);
};

}
