#pragma once

#include "guest/windows/session_boundary.hpp"
#include "proto/window_fields.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace rwin::guest {
class ProcessTree {
public:
    static constexpr std::size_t kMostDepth = 12;

    [[nodiscard]] std::vector<WindowFields::ProcessRef> tree_of(std::uint32_t pid);

private:
    struct Record {
        std::uint64_t created = 0;
        std::uint32_t parent = 0;
        bool boundary = false;
        std::uint64_t last_seen = 0;
    };
    using Key = std::pair<std::uint32_t, std::uint64_t>;

    std::optional<Record> learn(std::uint32_t pid);
    [[nodiscard]] std::optional<Record> remembered(std::uint32_t pid, std::uint64_t child_created) const;
    void learn_everything_alive();
    void keep(std::uint32_t pid, const Record& record);
    void prune();

    SessionBoundary boundary_;
    std::map<std::uint32_t, std::vector<Record>> records_;
    std::size_t kept_ = 0;
    std::uint64_t learned_all_at_ = 0;
    std::uint64_t last_snapshot_ = 0;
};

}
