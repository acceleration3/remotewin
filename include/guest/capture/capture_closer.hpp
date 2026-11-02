#pragma once

#include <chrono>
#include <memory>
#include <span>

namespace rwin::guest {

// Closes what a capture is done with - its session and its frame pool -
// on threads of its own, and waits a while for each close.
//
// Close is a call to the compositor's capture service, and one does not
// always come back: Word's key tips, some forty windows made and destroyed
// within a second, left a session's Close waiting on its reply for good,
// and with it the thread that captures - no frame of any window again, no
// window shown afterwards (the dialog it then opened, modal, was never
// mirrored). A close that has not finished in kPatience is left to its
// thread, which keeps whatever it was closing; the next is done on a new
// one. One that finishes in time keeps the order a capture depends on: a
// session on a window cannot be made while the last one is still
// capturing it.
class CaptureCloser {
public:
    static constexpr std::chrono::milliseconds kPatience{500};

    CaptureCloser();
    // Lets every thread finish what is queued and end on its own; one stuck
    // in a close stays stuck, and goes with the process.
    ~CaptureCloser();
    CaptureCloser(const CaptureCloser&) = delete;
    CaptureCloser& operator=(const CaptureCloser&) = delete;
    CaptureCloser(CaptureCloser&&) = delete;
    CaptureCloser& operator=(CaptureCloser&&) = delete;

    // Closes each of ``objects`` (WinRT objects, IClosable or not) in turn
    // and releases it; they are this one's from here, and null ones are
    // skipped.
    void close(std::span<void* const> objects);

private:
    struct Queue;
    void start_thread();

    std::shared_ptr<Queue> queue_;
};

}  // namespace rwin::guest
