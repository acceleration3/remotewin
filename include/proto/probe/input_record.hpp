#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// One input message a probe window's procedure was given, as it was given:
// the message, its two parameters, and what the procedure answered. Raw,
// so a later question about the same input needs no new field.
//
// Except WM_INPUT, whose lParam is a handle that is gone once the message
// is answered: its record carries what GetRawInputData read for a mouse -
// wparam the RAWMOUSE usFlags (MOUSE_MOVE_ABSOLUTE is 1) with
// usButtonFlags above bit 16, lparam lLastX in its low and lLastY in its
// high 32 bits.
struct InputRecord {
    std::uint32_t message = 0;  // WM_*
    std::uint64_t window = 0;
    std::uint64_t wparam = 0;
    std::uint64_t lparam = 0;
    // The procedure's return value: for WM_NCHITTEST, the hit-test code.
    std::int64_t outcome = 0;

    // The client coordinates a mouse message carries in its lparam, signed
    // as GET_X_LPARAM and GET_Y_LPARAM read them.
    [[nodiscard]] std::int32_t x() const noexcept {
        return static_cast<std::int16_t>(lparam & 0xFFFF);
    }
    [[nodiscard]] std::int32_t y() const noexcept {
        return static_cast<std::int16_t>((lparam >> 16) & 0xFFFF);
    }

    // A WM_INPUT record's motion, as RAWMOUSE has it.
    [[nodiscard]] std::int32_t raw_dx() const noexcept {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(lparam & 0xFFFFFFFF));
    }
    [[nodiscard]] std::int32_t raw_dy() const noexcept {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(lparam >> 32));
    }
    [[nodiscard]] bool raw_absolute() const noexcept { return (wparam & 1) != 0; }

    [[nodiscard]] bool operator==(const InputRecord&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<InputRecord> read(MessageReader& in);
};

}  // namespace rwin
