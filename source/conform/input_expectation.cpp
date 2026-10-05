#include "conform/input_expectation.hpp"

#include "proto/key_stroke.hpp"

#include <QStringList>

#include <array>

namespace rwin::conform {

namespace {

constexpr std::uint32_t kWmKeyDown = 0x0100;
constexpr std::uint32_t kWmKeyUp = 0x0101;
constexpr std::uint32_t kWmChar = 0x0102;
constexpr std::uint32_t kWmSysKeyDown = 0x0104;
constexpr std::uint32_t kWmSysKeyUp = 0x0105;
constexpr std::uint32_t kWmSysChar = 0x0106;
constexpr std::uint32_t kWmMouseMove = 0x0200;
constexpr std::uint32_t kWmLButtonDown = 0x0201;
constexpr std::uint32_t kWmLButtonUp = 0x0202;
constexpr std::uint32_t kWmRButtonDown = 0x0204;
constexpr std::uint32_t kWmRButtonUp = 0x0205;
constexpr std::uint32_t kWmMouseWheel = 0x020A;
constexpr std::uint32_t kVkF10 = 0x79;
constexpr const char* kDialogClass = "#32770";
// WS_EX_LAYERED | WS_EX_TRANSPARENT.
constexpr std::uint32_t kWsExLayeredTransparent = 0x00080000 | 0x00000020;
// lParam of a key message: the scan code from bit 16, the extended bit at
// 24, as nine bits KeyStroke packs the same way, prefix byte aside.
constexpr int kScanCodeShift = 16;
constexpr std::uint64_t kScanCodeBits = 0x1FF;
constexpr std::uint64_t kExtendedBit = 0x100;

std::uint64_t key_data(std::uint16_t scan_code) {
    const KeyStroke stroke{.scan_code = scan_code};
    return stroke.code() | (stroke.extended() ? kExtendedBit : 0);
}

std::uint64_t key_data_of(const InputRecord& record) {
    return (record.lparam >> kScanCodeShift) & kScanCodeBits;
}

bool is_key(std::uint32_t message) { return message >= kWmKeyDown && message <= kWmSysChar; }

struct MessageName {
    std::uint32_t message;
    const char* name;
};

constexpr std::array kNames{
    MessageName{0x0006, "WM_ACTIVATE"},      MessageName{0x0007, "WM_SETFOCUS"},
    MessageName{0x0008, "WM_KILLFOCUS"},     MessageName{0x0020, "WM_SETCURSOR"},
    MessageName{0x0021, "WM_MOUSEACTIVATE"}, MessageName{0x0084, "WM_NCHITTEST"},
    MessageName{kWmKeyDown, "WM_KEYDOWN"},   MessageName{kWmKeyUp, "WM_KEYUP"},
    MessageName{kWmChar, "WM_CHAR"},         MessageName{kWmSysKeyDown, "WM_SYSKEYDOWN"},
    MessageName{kWmSysKeyUp, "WM_SYSKEYUP"}, MessageName{kWmMouseMove, "WM_MOUSEMOVE"},
    MessageName{kWmLButtonDown, "WM_LBUTTONDOWN"}, MessageName{kWmLButtonUp, "WM_LBUTTONUP"},
    MessageName{0x0203, "WM_LBUTTONDBLCLK"}, MessageName{kWmRButtonDown, "WM_RBUTTONDOWN"},
    MessageName{kWmRButtonUp, "WM_RBUTTONUP"}, MessageName{0x0207, "WM_MBUTTONDOWN"},
    MessageName{0x0208, "WM_MBUTTONUP"},     MessageName{kWmMouseWheel, "WM_MOUSEWHEEL"},
    MessageName{0x020E, "WM_MOUSEHWHEEL"},   MessageName{0x0215, "WM_CAPTURECHANGED"},
    MessageName{0x0086, "WM_NCACTIVATE"},    MessageName{kWmSysChar, "WM_SYSCHAR"},
};

QString name_of(std::uint32_t message) {
    for (const MessageName& entry : kNames) {
        if (entry.message == message) return QString::fromLatin1(entry.name);
    }
    return QStringLiteral("message 0x%1").arg(message, 4, 16, QLatin1Char('0'));
}

bool is_mouse(std::uint32_t message) { return message >= kWmMouseMove && message <= 0x020E; }

}  // namespace

InputExpectation::InputExpectation(const Scenario::Step& step, const WindowTruth& window)
    : window_(window.handle), system_keys_(window.active && !window.focused) {
    if (!window.enabled) return;
    const bool click_through = (window.extended_style & kWsExLayeredTransparent) == kWsExLayeredTransparent;
    if (click_through && step.kind != Scenario::Step::Kind::Key) return;
    const std::int32_t x = step.client.x();
    const std::int32_t y = step.client.y();
    switch (step.kind) {
        case Scenario::Step::Kind::Move:
            expected_.push_back({kWmMouseMove, x, y, 0});
            break;
        case Scenario::Step::Kind::Click:
            // No WM_MOUSEMOVE: a pointer already over the point - the last
            // scenario's click left it there - moves nowhere, and Windows
            // says nothing. Where the button lands is the check.
            expected_.push_back({kWmLButtonDown, x, y, 0});
            expected_.push_back({kWmLButtonUp, x, y, 0});
            break;
        // Not input this window is sent as messages it journals: a resize,
        // the probe's own actions, what is done to the mirror on this side,
        // and the held pointer's motion (PointerExpectation's).
        case Scenario::Step::Kind::Resize:
        case Scenario::Step::Kind::Act:
        case Scenario::Step::Kind::HostMove:
        case Scenario::Step::Kind::Relative:
        case Scenario::Step::Kind::HostUnfocus:
        case Scenario::Step::Kind::HostFocus:
        case Scenario::Step::Kind::HostEscape:
        case Scenario::Step::Kind::HostRegrab:
            break;
        case Scenario::Step::Kind::Key:
            for (const Scenario::Key& held : step.held) expect_key(held, true);
            expect_key(step.key, true);
            // A dialog box's own modal loop runs IsDialogMessage, which
            // takes a character as a mnemonic unless the focus asks for
            // characters (DLGC_WANTCHARS); a dialog of no controls does
            // not, and is never given one.
            if (step.character != 0 && window.class_name != kDialogClass) {
                expected_.push_back({system_keys_ ? kWmSysChar : kWmChar, 0, 0, step.character, {}});
            }
            expect_key(step.key, false);
            for (auto held = step.held.rbegin(); held != step.held.rend(); ++held) {
                expect_key(*held, false);
            }
            break;
    }
}

// F10 is the key that opens the menu bar, and Windows sends it the way it
// sends a key pressed with Alt: as a system key.
void InputExpectation::expect_key(const Scenario::Key& key, bool pressed) {
    const bool system = key.virtual_key == kVkF10 || system_keys_;
    const std::uint32_t message = system ? (pressed ? kWmSysKeyDown : kWmSysKeyUp)
                                         : (pressed ? kWmKeyDown : kWmKeyUp);
    expected_.push_back({message, 0, 0, key.virtual_key, key.scan_code});
}

bool InputExpectation::matches(const Message& wanted, const InputRecord& record) const {
    if (record.window != window_ || record.message != wanted.message) return false;
    if (is_mouse(wanted.message)) return record.x() == wanted.x && record.y() == wanted.y;
    if (record.wparam != wanted.wparam) return false;
    return !wanted.scan_code.has_value() || key_data_of(record) == key_data(*wanted.scan_code);
}

bool InputExpectation::is_user_input(const InputRecord& record) const {
    if (record.window != window_) return false;
    return is_mouse(record.message) || is_key(record.message);
}

bool InputExpectation::satisfied_by(const std::vector<InputRecord>& records) const {
    if (expected_.empty()) return false;
    std::size_t next = 0;
    for (const InputRecord& record : records) {
        if (next < expected_.size() && matches(expected_[next], record)) ++next;
    }
    return next == expected_.size();
}

std::vector<InputExpectation::Check> InputExpectation::against(
    const std::vector<InputRecord>& records) const {
    std::vector<Check> checks;
    if (expected_.empty()) {
        QStringList given;
        for (const InputRecord& record : records) {
            if (is_user_input(record)) given.push_back(describe(record));
        }
        checks.push_back(Check{QStringLiteral("no mouse or keyboard messages"),
                               given.isEmpty() ? QStringLiteral("none") : given.join(QStringLiteral("; ")),
                               given.isEmpty()});
        return checks;
    }
    std::size_t at = 0;
    for (const Message& wanted : expected_) {
        std::size_t found = at;
        while (found < records.size() && !matches(wanted, records[found])) ++found;
        if (found < records.size()) {
            checks.push_back(Check{describe(wanted), describe(records[found]), true});
            at = found + 1;
            continue;
        }
        // The nearest thing to it: the same message, wherever it was.
        QString nearest = QStringLiteral("none");
        for (std::size_t i = at; i < records.size(); ++i) {
            if (records[i].window == window_ && records[i].message == wanted.message) {
                nearest = describe(records[i]);
                break;
            }
        }
        checks.push_back(Check{describe(wanted), nearest, false});
    }
    return checks;
}

QString InputExpectation::describe(const Message& message) const {
    InputRecord record;
    record.message = message.message;
    record.window = window_;
    record.wparam = message.wparam;
    record.lparam = is_mouse(message.message)
                        ? (static_cast<std::uint64_t>(static_cast<std::uint16_t>(message.y)) << 16)
                              | static_cast<std::uint16_t>(message.x)
                        : key_data(message.scan_code.value_or(0)) << kScanCodeShift;
    return describe(record);
}

QString InputExpectation::describe(const InputRecord& record) {
    const QString name = name_of(record.message);
    if (is_mouse(record.message)) return QStringLiteral("%1 at %2,%3").arg(name).arg(record.x()).arg(record.y());
    if (record.message == kWmChar || record.message == kWmSysChar) {
        return QStringLiteral("%1 0x%2").arg(name).arg(record.wparam, 2, 16, QLatin1Char('0'));
    }
    if (is_key(record.message)) {
        const std::uint64_t scan = key_data_of(record);
        return QStringLiteral("%1 vk 0x%2 sc %3%4")
            .arg(name)
            .arg(record.wparam, 2, 16, QLatin1Char('0'))
            .arg((scan & kExtendedBit) != 0 ? QStringLiteral("e0 ") : QString())
            .arg(scan & 0xFF, 2, 16, QLatin1Char('0'));
    }
    return name;
}

}  // namespace rwin::conform
