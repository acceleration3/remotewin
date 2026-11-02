#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rwin {
struct ProbeAction;
}

namespace rwin::probe {

class ProbeCursors;
class ProbeWindows;

class PointerActions {
public:
    PointerActions(ProbeWindows& windows, const ProbeCursors& cursors);

    [[nodiscard]] bool perform(const ProbeAction& action, std::string& failure);

    [[nodiscard]] std::int32_t display_count() const noexcept { return display_count_; }
    [[nodiscard]] std::uint64_t recentring() const noexcept { return recentring_; }
    [[nodiscard]] std::uint64_t put_back_by() const noexcept { return put_back_by_; }
    [[nodiscard]] std::uint32_t put_back() const;

    void reset();

private:
    [[nodiscard]] bool set_class_cursor(const ProbeAction& action, std::string& failure);
    [[nodiscard]] bool answer_cursor(const ProbeAction& action, std::string& failure);
    [[nodiscard]] bool clip(const ProbeAction& action, std::string& failure) const;
    [[nodiscard]] bool register_raw_input(const ProbeAction& action, std::string& failure);
    [[nodiscard]] bool recentre(const ProbeAction& action, std::string& failure);
    void show_cursor(bool show);
    [[nodiscard]] bool set_precision(bool on, std::string& failure);
    static void move_pointer(const ProbeAction& action);
    static void click(const ProbeAction& action);

    ProbeWindows& windows_;
    const ProbeCursors& cursors_;
    std::int32_t display_count_ = 0;
    std::int32_t resting_count_ = 0;
    bool raw_input_ = false;
    std::uint64_t recentring_ = 0;
    std::uint64_t put_back_by_ = 0;
    std::optional<std::array<int, 3>> ballistics_;
    std::vector<std::uint64_t> classes_changed_;
};

}
