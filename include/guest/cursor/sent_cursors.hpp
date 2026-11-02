#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rwin::guest {
class SentCursors {
public:
    static constexpr std::size_t kCapacity = 64;

    bool shown(std::uint64_t id);

private:
    std::vector<std::uint64_t> ids_;
};

}
