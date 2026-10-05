#include "conform/input_expectation.hpp"
#include "conform/scenario.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::InputRecord;
using rwin::conform::InputExpectation;
using rwin::conform::Scenario;

constexpr std::uint64_t kWindow = 0x500c4;
constexpr std::uint64_t kElsewhere = 0x600aa;

rwin::WindowTruth window(bool enabled = true, std::uint32_t extended_style = 0) {
    rwin::WindowTruth truth;
    truth.handle = kWindow;
    truth.enabled = enabled;
    truth.extended_style = extended_style;
    return truth;
}

InputRecord mouse(std::uint32_t message, int x, int y, std::uint64_t window = kWindow) {
    return InputRecord{message, window, 0,
                       (static_cast<std::uint64_t>(static_cast<std::uint16_t>(y)) << 16)
                           | static_cast<std::uint16_t>(x),
                       0};
}

// A key message as Windows makes one: the scan code at bit 16, the
// extended bit at 24.
InputRecord key(std::uint32_t message, std::uint64_t wparam, std::uint16_t scan_code = 0) {
    const std::uint64_t extended = (scan_code & 0xE000) != 0 ? 0x100 : 0;
    return InputRecord{message, kWindow, wparam, ((scan_code & 0xFFu) | extended) << 16, 0};
}

constexpr Scenario::Key kKeyA{Qt::Key_A, 30, 0x41, 0x1E};
constexpr Scenario::Key kRightCtrl{Qt::Key_Control, 97, 0x11, 0xE01D};
constexpr Scenario::Key kLeftShift{Qt::Key_Shift, 42, 0x10, 0x2A};

Scenario::Step click(int x, int y) {
    Scenario::Step step;
    step.kind = Scenario::Step::Kind::Click;
    step.client = QPoint(x, y);
    return step;
}

bool all_pass(const std::vector<InputExpectation::Check>& checks) {
    for (const auto& check : checks) {
        if (!check.passed) return false;
    }
    return !checks.empty();
}

void a_click_is_a_press_and_a_release_in_order() {
    const InputExpectation expected{click(271, 183), window()};
    // Hit tests and cursor queries in between are what Windows sends too.
    const std::vector<InputRecord> given{
        InputRecord{0x0084, kWindow, 0, 0, 1}, mouse(0x0200, 271, 183),
        InputRecord{0x0020, kWindow, 0, 0, 0}, mouse(0x0201, 271, 183), mouse(0x0202, 271, 183)};
    check(expected.satisfied_by(given), "a click delivered whole satisfies");
    check(all_pass(expected.against(given)), "and every message passes");
    // A pointer that was already there has no move to report.
    const std::vector<InputRecord> still{mouse(0x0201, 271, 183), mouse(0x0202, 271, 183)};
    check(expected.satisfied_by(still), "a click without a move still satisfies");
}

void a_click_eight_pixels_off_names_where_it_landed() {
    const InputExpectation expected{click(271, 183), window()};
    const std::vector<InputRecord> given{mouse(0x0200, 263, 183), mouse(0x0201, 263, 183),
                                         mouse(0x0202, 263, 183)};
    check(!expected.satisfied_by(given), "a click elsewhere does not satisfy");
    const auto checks = expected.against(given);
    check(checks.size() == 2 && !checks[0].passed, "the press is reported missing");
    check(checks.size() == 2 && checks[0].actual == QStringLiteral("WM_LBUTTONDOWN at 263,183"),
          "with the press that did come, and where");
}

void order_matters() {
    const InputExpectation expected{click(10, 10), window()};
    const std::vector<InputRecord> given{mouse(0x0202, 10, 10), mouse(0x0201, 10, 10)};
    check(!expected.satisfied_by(given), "a release before the press is not a click");
}

void another_windows_input_is_not_this_ones() {
    const InputExpectation expected{click(10, 10), window()};
    const std::vector<InputRecord> given{mouse(0x0200, 10, 10, kElsewhere),
                                         mouse(0x0201, 10, 10, kElsewhere),
                                         mouse(0x0202, 10, 10, kElsewhere)};
    check(!expected.satisfied_by(given), "input to another window does not count");
}

void a_letter_is_its_virtual_key_and_its_character() {
    Scenario::Step step;
    step.kind = Scenario::Step::Kind::Key;
    step.key = kKeyA;
    step.character = 'a';
    const InputExpectation expected{step, window()};
    const std::vector<InputRecord> keyboard{key(0x0100, 0x41, 0x1E), key(0x0102, 'a', 0x1E),
                                            key(0x0101, 0x41, 0x1E)};
    check(all_pass(expected.against(keyboard)), "WM_KEYDOWN 'A', WM_CHAR 'a', WM_KEYUP 'A'");
    // What SendInput with KEYEVENTF_UNICODE delivers instead.
    const std::vector<InputRecord> packet{key(0x0100, 0xE7), key(0x0102, 'a'), key(0x0101, 0xE7)};
    const auto checks = expected.against(packet);
    check(!all_pass(checks), "a VK_PACKET is not the key that was pressed");
    check(checks.size() == 3 && checks[1].passed, "even though the character is right");
    const std::vector<InputRecord> unscanned{key(0x0100, 0x41), key(0x0102, 'a'), key(0x0101, 0x41)};
    check(!all_pass(expected.against(unscanned)), "the right key with no scan code is not a keystroke");
}

void modifiers_go_down_first_and_up_last() {
    Scenario::Step step;
    step.kind = Scenario::Step::Kind::Key;
    step.held = {kRightCtrl, kLeftShift};
    step.key = kKeyA;
    step.character = 0x01;
    const InputExpectation expected{step, window()};
    const std::vector<InputRecord> keyboard{
        key(0x0100, 0x11, 0xE01D), key(0x0100, 0x10, 0x2A), key(0x0100, 0x41, 0x1E),
        key(0x0102, 0x01, 0x1E),   key(0x0101, 0x41, 0x1E), key(0x0101, 0x10, 0x2A),
        key(0x0101, 0x11, 0xE01D)};
    check(all_pass(expected.against(keyboard)), "Ctrl, Shift, A, ^A, and back up in reverse");
    std::vector<InputRecord> left_ctrl = keyboard;
    left_ctrl.front() = key(0x0100, 0x11, 0x1D);
    check(!all_pass(expected.against(left_ctrl)),
          "the left Ctrl is not the right one: the extended bit tells them apart");
}

void the_pointer_passes_through_a_click_through_window() {
    const InputExpectation expected{click(10, 10), window(true, 0x00080020)};
    check(expected.expects_nothing(), "WS_EX_LAYERED | WS_EX_TRANSPARENT is given no pointer input");
    Scenario::Step typed;
    typed.kind = Scenario::Step::Kind::Key;
    typed.key = kKeyA;
    check(!InputExpectation{typed, window(true, 0x00080020)}.expects_nothing(), "but keys still");
    check(!InputExpectation{click(10, 10), window(true, 0x00000020)}.expects_nothing(),
          "WS_EX_TRANSPARENT alone does not let the pointer through");
}

void a_disabled_window_is_given_nothing() {
    const InputExpectation expected{click(10, 10), window(false)};
    check(expected.expects_nothing(), "a disabled window expects no messages");
    check(!expected.satisfied_by({}), "and is never done waiting early");
    const std::vector<InputRecord> quiet{InputRecord{0x0084, kWindow, 0, 0, -2}};
    check(all_pass(expected.against(quiet)), "a hit test is not input");
    const std::vector<InputRecord> pressed{mouse(0x0201, 10, 10)};
    check(!all_pass(expected.against(pressed)), "a press reaching it fails");
}

}  // namespace

int main() {
    a_click_is_a_press_and_a_release_in_order();
    a_click_eight_pixels_off_names_where_it_landed();
    order_matters();
    another_windows_input_is_not_this_ones();
    a_letter_is_its_virtual_key_and_its_character();
    modifiers_go_down_first_and_up_last();
    a_disabled_window_is_given_nothing();
    the_pointer_passes_through_a_click_through_window();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input expectation: all checks passed\n");
    return EXIT_SUCCESS;
}
