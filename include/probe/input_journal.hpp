#pragma once

#include "proto/probe/input_record.hpp"

#include <cstddef>
#include <vector>

namespace rwin::probe {
class InputJournal {
public:
    void note(const InputRecord& record);

    [[nodiscard]] std::vector<InputRecord> drain();

private:
    static constexpr std::size_t kCapacity = 32768;

    std::vector<InputRecord> records_;
    std::size_t dropped_ = 0;
};

}
