#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::host {
class BusConnection;
}

namespace rwin::integration {
class ScreenCapture {
public:
    struct Picture {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;

        [[nodiscard]] std::uint32_t at(int x, int y) const noexcept;
    };

    explicit ScreenCapture(host::BusConnection& bus) : bus_(bus) {}

    [[nodiscard]] std::optional<Picture> area(int x, int y, int width, int height);

private:
    host::BusConnection& bus_;
};

}
