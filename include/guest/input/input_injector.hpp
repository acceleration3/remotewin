#pragma once

#include <cstdint>

namespace rwin::guest {
class InputBatch;

class InputInjector {
public:
    std::uint32_t inject(const InputBatch& batch) const;
};

}
