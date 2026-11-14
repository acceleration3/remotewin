#pragma once

#include "host/geometry/size.hpp"
#include "host/wayland/toplevel_configure.hpp"

#include <optional>

namespace rwin::host {
class SettledRoom {
public:
    [[nodiscard]] std::optional<Size> changed(const ToplevelConfigure& configure, double scale) noexcept;

private:
    Size told_;
};

}
