#include "guest/input/input_port.hpp"

#include "guest/input/input_batch.hpp"
#include "logging/logger.hpp"

namespace rwin::guest {

namespace {
constexpr auto kRetryEvery = std::chrono::seconds(5);
}

InputPort& InputPort::shared() {
    static InputPort port;
    return port;
}

bool InputPort::relay_ready() {
    if (relay_.connected()) return true;
    const auto now = std::chrono::steady_clock::now();
    if (now < retry_at_) return false;
    std::uint32_t error = 0;
    if (relay_.connect(error)) {
        RWIN_INFO("input", "connected to the input relay; input goes through it");
        fallback_logged_ = false;
        return true;
    }
    retry_at_ = now + kRetryEvery;
    if (!fallback_logged_) {
        RWIN_WARN("input", "no input relay (error {}); injecting input directly, which cannot reach "
                  "windows above this process's integrity level", error);
        fallback_logged_ = true;
    }
    return false;
}

std::uint32_t InputPort::send(const InputBatch& batch) {
    const std::lock_guard lock{mutex_};
    if (relay_ready()) {
        std::uint32_t error = 0;
        if (relay_.write(batch, error)) return static_cast<std::uint32_t>(batch.records().size());
        RWIN_WARN("input", "the input relay stopped taking input (error {}); injecting directly", error);
        fallback_logged_ = true;
        retry_at_ = std::chrono::steady_clock::now() + kRetryEvery;
    }
    return injector_.inject(batch);
}

}
