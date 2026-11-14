#pragma once

#include "host/geometry/size.hpp"
#include "host/wayland/toplevel_configure.hpp"

#include <optional>

namespace rwin::host {
struct ConfigureMeaning {
    enum class Kind { Free, Placed, Maximized, Fullscreen };

    struct Guest {
        Size size;
        Size smallest;
        Size largest;
        bool sizable = false;
    };

    Kind kind = Kind::Free;
    std::optional<Size> guest_size;

    [[nodiscard]] bool held() const noexcept { return kind == Kind::Maximized || kind == Kind::Fullscreen; }

    [[nodiscard]] static ConfigureMeaning of(const ToplevelConfigure& configure, const Guest& guest) noexcept;
};

}
