#pragma once

#include "guest/input/input_injector.hpp"
#include "guest/input/input_relay_client.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>

namespace rwin::guest {
class InputBatch;

class InputPort {
public:
    static InputPort& shared();

    std::uint32_t send(const InputBatch& batch);

private:
    InputPort() = default;

    bool relay_ready();

    std::mutex mutex_;
    InputRelayClient relay_;
    InputInjector injector_;
    std::chrono::steady_clock::time_point retry_at_{};
    bool fallback_logged_ = false;
};

}
