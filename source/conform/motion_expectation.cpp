#include "conform/motion_expectation.hpp"

#include <cstdlib>

namespace rwin::conform {

namespace {

constexpr std::uint32_t kWmMouseMove = 0x0200;
constexpr std::uint32_t kWmInput = 0x00FF;
constexpr std::uint32_t kRidevNoLegacy = 0x30;

int sign(int value) { return (value > 0) - (value < 0); }

QString point_text(QPoint point) { return QStringLiteral("%1,%2").arg(point.x()).arg(point.y()); }

}  // namespace

MotionExpectation::MotionExpectation(QPoint motion, const WindowTruth& window, const CursorTruth& before)
    : motion_(motion),
      window_(window.handle),
      raw_(before.raw_mouse && (before.raw_target == 0 || before.raw_target == window.handle)),
      legacy_(!before.raw_mouse || (before.raw_flags & kRidevNoLegacy) != kRidevNoLegacy) {
    put_back_ = before.recentring == window.handle;
    start_ = put_back_ ? QPoint(window.client.width() / 2, window.client.height() / 2)
                       : QPoint(before.x - window.client.left, before.y - window.client.top);
    held_ = before.clipped && ((motion.x() != 0 && before.clip.width() <= 1) || (motion.y() != 0 && before.clip.height() <= 1));
}

QPoint MotionExpectation::raw_motion(const std::vector<InputRecord>& records, int& messages) const {
    QPoint total;
    messages = 0;
    for (const InputRecord& record : records) {
        if (record.window != window_ || record.message != kWmInput || record.raw_absolute()) continue;
        total += QPoint(record.raw_dx(), record.raw_dy());
        ++messages;
    }
    return total;
}

std::optional<QPoint> MotionExpectation::legacy_move(const std::vector<InputRecord>& records) const {
    std::optional<QPoint> moved;
    for (const InputRecord& record : records) {
        if (record.window != window_ || record.message != kWmMouseMove) continue;
        const QPoint at(record.x(), record.y());
        if (at == start_) continue;
        moved = put_back_ ? moved.value_or(QPoint()) + (at - start_) : at - start_;
    }
    return moved;
}

bool MotionExpectation::legacy_as_wanted(const std::optional<QPoint>& moved) const {
    if (held_) return !moved.has_value();
    return moved.has_value() && sign(moved->x()) == sign(motion_.x()) && sign(moved->y()) == sign(motion_.y());
}

bool MotionExpectation::satisfied_by(const std::vector<InputRecord>& records) const {
    int messages = 0;
    const QPoint raw = raw_motion(records, messages);
    const bool raw_done = !raw_ || (std::abs(raw.x() - motion_.x()) <= 1 && std::abs(raw.y() - motion_.y()) <= 1);
    const bool legacy_done = !legacy_ || held_ || legacy_as_wanted(legacy_move(records));
    return raw_done && legacy_done;
}

std::vector<InputExpectation::Check> MotionExpectation::against(const std::vector<InputRecord>& records) const {
    std::vector<InputExpectation::Check> checks;
    int messages = 0;
    const QPoint raw = raw_motion(records, messages);
    if (raw_) {
        checks.push_back({QStringLiteral("WM_INPUT motion %1 in all (within a count)").arg(point_text(motion_)),
                          QStringLiteral("%1 WM_INPUT, motion %2").arg(messages).arg(point_text(raw)),
                          std::abs(raw.x() - motion_.x()) <= 1 && std::abs(raw.y() - motion_.y()) <= 1});
    } else if (messages != 0) {
        checks.push_back({QStringLiteral("no WM_INPUT: raw input not registered"),
                          QStringLiteral("%1 WM_INPUT").arg(messages), false});
    }
    if (!legacy_) return checks;
    const std::optional<QPoint> moved = legacy_move(records);
    const QString expected = held_ ? QStringLiteral("no WM_MOUSEMOVE away from %1: the clip holds the cursor").arg(point_text(start_))
                                   : QStringLiteral("WM_MOUSEMOVE away from %1 the way of %2").arg(point_text(start_), point_text(motion_));
    const QString actual = moved.has_value()
                               ? QStringLiteral("WM_MOUSEMOVE moved by %1 in all (motion %2)").arg(point_text(*moved), point_text(motion_))
                               : QStringLiteral("no WM_MOUSEMOVE away from %1").arg(point_text(start_));
    checks.push_back({expected, actual, legacy_as_wanted(moved)});
    return checks;
}

}  // namespace rwin::conform
