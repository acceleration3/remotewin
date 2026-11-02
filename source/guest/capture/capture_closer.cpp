#include "guest/capture/capture_closer.hpp"

#include "guest/capture/winrt_close.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <roapi.h>
// clang-format on

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace rwin::guest {

namespace {

struct Closing {
    std::vector<void*> objects;
    bool finished = false;
};

}  // namespace

struct CaptureCloser::Queue {
    std::mutex lock;
    std::condition_variable work;
    std::condition_variable finished;
    std::deque<std::shared_ptr<Closing>> closings;
    bool stopping = false;

    // Until stopping, with nothing left to close.
    void close_queued();
};

namespace {

void close_and_release(void* object) {
    close_winrt(object);
    static_cast<IUnknown*>(object)->Release();
}

}  // namespace

// The objects were made in the multithreaded apartment, and are closed
// from it.
void CaptureCloser::Queue::close_queued() {
    const HRESULT initialised = ::RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialised) && initialised != RPC_E_CHANGED_MODE) {
        RWIN_ERROR("capture", "the capture closer has no apartment: {:#010x}", static_cast<unsigned>(initialised));
    }
    std::unique_lock held{lock};
    for (;;) {
        work.wait(held, [this] { return stopping || !closings.empty(); });
        if (closings.empty()) break;
        const std::shared_ptr<Closing> closing = std::move(closings.front());
        closings.pop_front();
        held.unlock();
        for (void* object : closing->objects) close_and_release(object);
        held.lock();
        closing->finished = true;
        finished.notify_all();
    }
    if (SUCCEEDED(initialised)) ::RoUninitialize();
}

CaptureCloser::CaptureCloser() : queue_(std::make_shared<Queue>()) { start_thread(); }

CaptureCloser::~CaptureCloser() {
    {
        const std::lock_guard held{queue_->lock};
        queue_->stopping = true;
    }
    queue_->work.notify_all();
}

// Detached: a thread stuck in a close cannot be joined, and the queue it
// shares outlives this.
void CaptureCloser::start_thread() {
    std::thread([queue = queue_] { queue->close_queued(); }).detach();
}

void CaptureCloser::close(std::span<void* const> objects) {
    auto closing = std::make_shared<Closing>();
    for (void* object : objects) {
        if (object != nullptr) closing->objects.push_back(object);
    }
    if (closing->objects.empty()) return;
    std::unique_lock held{queue_->lock};
    queue_->closings.push_back(closing);
    queue_->work.notify_one();
    if (queue_->finished.wait_for(held, kPatience, [&] { return closing->finished; })) return;
    held.unlock();
    RWIN_WARN("capture", "a capture has not closed in {} ms; going on without it, on a new closing thread",
              kPatience.count());
    start_thread();
}

}  // namespace rwin::guest
