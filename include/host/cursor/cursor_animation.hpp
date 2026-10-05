#pragma once

#include <QObject>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rwin::host {

// The steps of an animated cursor, each shown for its own time, round and
// round: what Windows does with an .ani cursor's rate chunk.
class CursorAnimation : public QObject {
    Q_OBJECT
public:
    explicit CursorAnimation(QObject* parent = nullptr);

    // From the first step; fewer than two is a still cursor, and stops it.
    void play(std::vector<int> delays_ms);

    [[nodiscard]] std::size_t step() const noexcept { return step_; }
    // Steps moved on since play: an animation that is running is one whose
    // count goes up.
    [[nodiscard]] std::uint64_t steps_taken() const noexcept { return taken_; }

Q_SIGNALS:
    void stepped(std::size_t step);

private:
    void advance();

    // A step of no time is shown for a jiffy, as Windows shows it.
    static constexpr int kShortestMs = 16;

    QTimer timer_;
    std::vector<int> delays_;
    std::size_t step_ = 0;
    std::uint64_t taken_ = 0;
};

}  // namespace rwin::host
