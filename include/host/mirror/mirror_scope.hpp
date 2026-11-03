#pragma once

#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rwin::host {

class GuestWindow;
class GuestWindows;

class MirrorScope {
public:
    struct Launch {
        std::optional<std::uint32_t> process_id;
        std::optional<std::uint64_t> process_created;
        std::string image;
        std::vector<std::string> aumids;
        bool same_app_id = true;
    };

    explicit MirrorScope(Launch launch);

    [[nodiscard]] bool everything() const noexcept;

    [[nodiscard]] std::vector<std::string> claim(const GuestWindows& windows);
    [[nodiscard]] bool belongs(WindowId window, const GuestWindows& windows) const;
    [[nodiscard]] const std::vector<std::string>& claimed() const noexcept { return claimed_; }

private:
    [[nodiscard]] bool launched(const GuestWindow& window) const;
    [[nodiscard]] bool belongs_itself(const GuestWindow& window) const;
    [[nodiscard]] bool is_launched(const WindowFields::ProcessRef& process) const;
    [[nodiscard]] bool in_launched_tree(const WindowFields::AppId& app) const;
    [[nodiscard]] bool holds_launched(const std::vector<WindowFields::ProcessRef>& tree) const;

    Launch launch_;
    std::string image_;
    std::vector<std::string> claimed_;
    std::optional<WindowFields::ProcessRef> tree_root_;
    bool tree_claimed_ = false;
};

}
