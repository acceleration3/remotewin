#pragma once

#include <chrono>
#include <memory>
#include <span>

namespace rwin::guest {
class CaptureCloser {
public:
    static constexpr std::chrono::milliseconds kPatience{500};

    CaptureCloser();
    ~CaptureCloser();
    CaptureCloser(const CaptureCloser&) = delete;
    CaptureCloser& operator=(const CaptureCloser&) = delete;
    CaptureCloser(CaptureCloser&&) = delete;
    CaptureCloser& operator=(CaptureCloser&&) = delete;

    void close(std::span<void* const> objects);

private:
    struct Queue;
    void start_thread();

    std::shared_ptr<Queue> queue_;
};

}
