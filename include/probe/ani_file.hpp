#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rwin::probe {

// An animated cursor file (.ani: RIFF "ACON", an "anih" header, a "rate"
// chunk and a list of .cur frames), built in memory and written out, so
// the probe loads its animated cursor the way an application loads one it
// ships: from a file.
class AniFile {
public:
    struct Frame {
        std::uint32_t side = 0;
        std::uint32_t hotspot_x = 0;
        std::uint32_t hotspot_y = 0;
        // Sixtieths of a second.
        std::uint32_t jiffies = 0;
        // Straight BGRA, top-down, side * side.
        std::vector<std::uint8_t> bgra;
    };

    void add(Frame frame);

    // False when the file cannot be written; ``failure`` says why.
    [[nodiscard]] bool write(const std::wstring& path, std::string& failure) const;

private:
    [[nodiscard]] static std::vector<std::uint8_t> cur_bytes(const Frame& frame);

    std::vector<Frame> frames_;
};

}  // namespace rwin::probe
