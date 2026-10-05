#pragma once

#include "conform/scenario.hpp"
#include "proto/probe/input_record.hpp"
#include "proto/probe/window_truth.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin::conform {

// What one step of input has to arrive as in the guest window: the
// messages Windows gives a window when a real mouse or keyboard does the
// same thing, in that order.
//
// The window's journal holds more than these - hit tests, cursor queries,
// activation - and those are allowed: the expected messages need only
// appear in it in order. A window that takes no input is expected to be
// given none of the messages a pointer or a key produces.
class InputExpectation {
public:
    struct Check {
        QString expected;
        QString actual;
        bool passed = false;
    };

    // ``window`` is what Windows says the window is: a disabled one
    // (IsWindowEnabled) is given no input, and the pointer passes through
    // a WS_EX_LAYERED | WS_EX_TRANSPARENT one to whatever is beneath.
    InputExpectation(const Scenario::Step& step, const WindowTruth& window);

    // True once ``records`` hold everything expected, so waiting for more
    // can stop. A step that expects nothing is never satisfied early.
    [[nodiscard]] bool satisfied_by(const std::vector<InputRecord>& records) const;

    [[nodiscard]] std::vector<Check> against(const std::vector<InputRecord>& records) const;

    [[nodiscard]] bool expects_nothing() const noexcept { return expected_.empty(); }

    // A Windows message as a reader wants to see it.
    [[nodiscard]] static QString describe(const InputRecord& record);

private:
    struct Message {
        std::uint32_t message = 0;
        // Client coordinates for a mouse message; the virtual key or the
        // character for a keyboard one.
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint64_t wparam = 0;
        // For a key going down or up: the scan code and extended bit
        // lParam carries in bits 16-24, as KeyStroke writes a scan code.
        std::optional<std::uint16_t> scan_code{};
    };

    void expect_key(const Scenario::Key& key, bool pressed);
    [[nodiscard]] bool matches(const Message& wanted, const InputRecord& record) const;
    [[nodiscard]] bool is_user_input(const InputRecord& record) const;
    [[nodiscard]] QString describe(const Message& message) const;

    std::uint64_t window_;
    // The window is active and nothing in it has the focus - EnableWindow
    // took it away - so its keys come as system keys: "If no window has
    // the keyboard focus, the system posts WM_SYSKEYDOWN to the active
    // window", and TranslateMessage makes WM_SYSCHAR of them.
    bool system_keys_ = false;
    std::vector<Message> expected_;
};

}  // namespace rwin::conform
