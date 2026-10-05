#pragma once

#include "proto/probe/input_record.hpp"

#include <cstddef>
#include <vector>

namespace rwin::probe {

// Every input message the probe's windows were given, in the order they
// were given it, until the harness collects them.
class InputJournal {
public:
    // Kept only when it is input - the pointer, the keyboard, and the
    // activation, focus and cursor traffic that comes with them; painting
    // and sizing are not.
    void note(const InputRecord& record);

    // Everything noted since the last drain, oldest first.
    [[nodiscard]] std::vector<InputRecord> drain();

private:
    // A pointer resting over a window is a hit test and a cursor query
    // every few milliseconds. A harness that never drains is not allowed
    // to grow the probe without bound; past this the newest are dropped,
    // and said to be.
    static constexpr std::size_t kCapacity = 32768;

    std::vector<InputRecord> records_;
    std::size_t dropped_ = 0;
};

}  // namespace rwin::probe
