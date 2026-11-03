#include "guest/input/key_input.hpp"

#include "guest/input/input_batch.hpp"
#include "guest/input/input_port.hpp"
#include "logging/logger.hpp"
#include "proto/key_stroke.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <initializer_list>

namespace rwin::guest {

namespace {
struct ScanCode {
    WORD value = 0;
    bool extended = false;
};

ScanCode scan_code_of(std::uint32_t virtual_key) {
    const UINT mapped = ::MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC_EX);
    const UINT prefix = mapped >> 8;
    return {static_cast<WORD>(mapped & 0xFF), prefix == 0xE0 || prefix == 0xE1};
}

UINT send(const InputBatch& batch) { return InputPort::shared().send(batch); }

void type(std::uint32_t character) {
    InputBatch batch;
    if (character > 0xFFFF) {
        const std::uint32_t offset = character - 0x10000;
        for (const WORD half : {static_cast<WORD>(0xD800 + (offset >> 10)),
                                static_cast<WORD>(0xDC00 + (offset & 0x3FF))}) {
            batch.add_key(0, half, KEYEVENTF_UNICODE);
            batch.add_key(0, half, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP);
        }
        send(batch);
        return;
    }
    const auto unit = static_cast<WORD>(character);
    batch.add_key(0, unit, KEYEVENTF_UNICODE);
    batch.add_key(0, unit, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP);
    RWIN_DEBUG("input", "typed {:#x}: {} of 2 events accepted", character, send(batch));
}

void press_scan_code(std::uint16_t scan_code, bool pressed) {
    DWORD flags = KEYEVENTF_SCANCODE | (pressed ? 0 : KEYEVENTF_KEYUP);
    if ((scan_code & KeyStroke::kExtended) == KeyStroke::kExtended) {
        flags |= KEYEVENTF_EXTENDEDKEY;
    }
    InputBatch batch;
    batch.add_key(0, static_cast<WORD>(scan_code & 0xFF), flags);
    const UINT accepted = send(batch);
    RWIN_DEBUG("input", "scan code {:#06x} {}: {} accepted", scan_code,
               pressed ? "down" : "up", accepted);
}

void press_virtual_key(std::uint32_t virtual_key, bool pressed) {
    const ScanCode scan = scan_code_of(virtual_key);
    DWORD flags = pressed ? 0 : KEYEVENTF_KEYUP;
    if (scan.extended) flags |= KEYEVENTF_EXTENDEDKEY;
    InputBatch batch;
    batch.add_key(static_cast<WORD>(virtual_key), scan.value, flags);
    const UINT accepted = send(batch);
    RWIN_DEBUG("input", "virtual key {:#x} {}: {} accepted", virtual_key,
               pressed ? "down" : "up", accepted);
}

}

void put_key(const KeyStroke& stroke) {
    if (stroke.scan_code != 0) {
        press_scan_code(stroke.scan_code, stroke.pressed);
    } else if (stroke.character != 0) {
        if (stroke.pressed) type(stroke.character);
    } else if (stroke.virtual_key != 0) {
        press_virtual_key(stroke.virtual_key, stroke.pressed);
    }
}

}
