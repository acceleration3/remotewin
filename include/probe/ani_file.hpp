#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rwin::probe {
class AniFile {
public:
    struct Frame {
        std::uint32_t side = 0;
        std::uint32_t hotspot_x = 0;
        std::uint32_t hotspot_y = 0;
        std::uint32_t jiffies = 0;
        std::vector<std::uint8_t> bgra;
    };

    void add(Frame frame);

    [[nodiscard]] bool write(const std::wstring& path, std::string& failure) const;

private:
    [[nodiscard]] static std::vector<std::uint8_t> cur_bytes(const Frame& frame);

    std::vector<Frame> frames_;
};

}
