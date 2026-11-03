#pragma once

#include "guest/windows/kernel_handle.hpp"

#include <cstdint>

namespace rwin::guest {
class InputBatch;

class InputRelayClient {
public:
    InputRelayClient();

    [[nodiscard]] bool connect(std::uint32_t& error);
    [[nodiscard]] bool connected() const noexcept { return pipe_.valid(); }
    [[nodiscard]] bool write(const InputBatch& batch, std::uint32_t& error);

private:
    std::uint32_t session_ = 0;
    KernelHandle pipe_;
};

}
