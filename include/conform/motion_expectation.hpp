#pragma once

#include "conform/input_expectation.hpp"
#include "proto/probe/cursor_truth.hpp"
#include "proto/probe/input_record.hpp"
#include "proto/probe/window_truth.hpp"

#include <QPoint>

#include <optional>
#include <vector>

namespace rwin::conform {

// What the held pointer's motion has to arrive as in the guest: as raw
// input, the very counts (a mouse's motion reaches WM_INPUT unaccelerated),
// and - unless the window asked for no legacy messages - as the cursor
// moving the same way (WM_MOUSEMOVE), by however much Windows' pointer
// speed and "Enhance pointer precision" make of it.
class MotionExpectation {
public:
    // ``before`` is the cursor as it was before the motion: where it was,
    // whether it is clipped, read raw, put back.
    MotionExpectation(QPoint motion, const WindowTruth& window, const CursorTruth& before);

    [[nodiscard]] bool satisfied_by(const std::vector<InputRecord>& records) const;
    [[nodiscard]] std::vector<InputExpectation::Check> against(const std::vector<InputRecord>& records) const;

private:
    [[nodiscard]] QPoint raw_motion(const std::vector<InputRecord>& records, int& messages) const;
    // How far the cursor was moved in all: each move away from the middle
    // of a window that puts it back there, added up, or the last place it
    // was moved to otherwise, from where it started; nothing when it never
    // left it.
    [[nodiscard]] std::optional<QPoint> legacy_move(const std::vector<InputRecord>& records) const;
    [[nodiscard]] bool legacy_as_wanted(const std::optional<QPoint>& moved) const;

    QPoint motion_;
    std::uint64_t window_ = 0;
    // Client coordinates the cursor starts from: the middle of a window
    // that puts it back there, where it was otherwise.
    QPoint start_;
    bool put_back_ = false;
    bool raw_ = false;
    bool legacy_ = true;
    // Clipped too tightly to move along an axis the motion moves on.
    bool held_ = false;
};

}  // namespace rwin::conform
