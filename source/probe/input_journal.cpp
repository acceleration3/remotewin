#include "probe/input_journal.hpp"

#include "logging/logger.hpp"

#include <utility>

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::probe {

namespace {

// The input a window can be given, and the activation and cursor queries
// that come with it. Painting and sizing are not input.
bool is_input(std::uint32_t message) {
    switch (message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_SETCURSOR:
        case WM_NCHITTEST:
        case WM_ACTIVATE:
        case WM_MOUSEACTIVATE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_CAPTURECHANGED:
        case WM_NCACTIVATE:
        case WM_INPUT:
            return true;
        default:
            return false;
    }
}

}  // namespace

void InputJournal::note(const InputRecord& record) {
    if (!is_input(record.message)) return;
    if (records_.size() >= kCapacity) {
        ++dropped_;
        return;
    }
    records_.push_back(record);
}

std::vector<InputRecord> InputJournal::drain() {
    if (dropped_ != 0) {
        RWIN_WARN("input", "{} input message(s) dropped since the last drain", dropped_);
        dropped_ = 0;
    }
    std::vector<InputRecord> drained;
    drained.swap(records_);
    return drained;
}

}  // namespace rwin::probe
