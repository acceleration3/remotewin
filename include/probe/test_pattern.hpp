#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::probe {
class TestPattern {
public:
    void paint(void* device_context, std::int32_t width, std::int32_t height,
               std::optional<std::uint32_t> key);

private:
    void fill(std::int32_t width, std::int32_t height, std::optional<std::uint32_t> key);

    std::vector<std::uint32_t> pixels_;
};

}
