#include "guest/input/input_injector.hpp"

#include "guest/input/input_batch.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

std::uint32_t InputInjector::inject(const InputBatch& batch) const {
    INPUT inputs[InputBatch::kMaxRecords] = {};
    UINT count = 0;
    for (const InputBatch::Record& record : batch.records()) {
        INPUT& input = inputs[count++];
        if (record.kind == InputBatch::Record::Kind::Mouse) {
            input.type = INPUT_MOUSE;
            input.mi.dx = record.dx;
            input.mi.dy = record.dy;
            input.mi.mouseData = record.mouse_data;
            input.mi.dwFlags = record.flags;
        } else {
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = record.virtual_key;
            input.ki.wScan = record.scan_code;
            input.ki.dwFlags = record.flags;
        }
    }
    if (count == 0) return 0;
    const UINT accepted = ::SendInput(count, inputs, sizeof(INPUT));
    if (accepted != count) {
        RWIN_WARN("input", "{} of {} input events accepted (error {})", accepted, count, ::GetLastError());
    }
    return accepted;
}

}
