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

// Does to the cursor and the pointer what an application does: sets a
// window's cursor, hides the cursor, clips it, captures it, asks for raw
// mouse input, puts it back at a point on every move - and moves or clicks
// the pointer itself through SendInput, as a user's mouse would.
//
// The cursor and the pointer are the desktop's, not a window's: what one
// scenario did to them outlives its windows, and is undone (reset) before
// the next.
class PointerActions {
public:
    PointerActions(ProbeWindows& windows, const ProbeCursors& cursors);

    // False when Windows refused, with ``failure`` saying why.
    [[nodiscard]] bool perform(const ProbeAction& action, std::string& failure);

    // ShowCursor's counter as Windows last answered it: only the probe's
    // thread calls it, so the last answer is the count.
    [[nodiscard]] std::int32_t display_count() const noexcept { return display_count_; }
    // The window that puts the cursor back on every move, 0 for none.
    [[nodiscard]] std::uint64_t recentring() const noexcept { return recentring_; }
    // The window last set to put the cursor back, and how many moves in a
    // row it has put back.
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
    // What it was before any scenario: 0 with a mouse, -1 without one.
    std::int32_t resting_count_ = 0;
    bool raw_input_ = false;
    std::uint64_t recentring_ = 0;
    std::uint64_t put_back_by_ = 0;
    // SPI_GETMOUSE before a scenario changed it: thresholds, acceleration.
    std::optional<std::array<int, 3>> ballistics_;
    // Windows whose class cursor was changed: a class outlives its windows,
    // and the next scenario's are of the same classes.
    std::vector<std::uint64_t> classes_changed_;
};

}  // namespace rwin::probe
