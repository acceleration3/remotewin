#pragma once

namespace rwin::guest {

// The calling thread, for as long as this lives, scheduled by the
// Multimedia Class Scheduler Service as one that has to keep time: it runs
// ahead of ordinary threads when it wakes, so a frame or a key waits on
// nothing else in the guest.
class MultimediaThread {
public:
    MultimediaThread();
    ~MultimediaThread();

    MultimediaThread(const MultimediaThread&) = delete;
    MultimediaThread& operator=(const MultimediaThread&) = delete;
    MultimediaThread(MultimediaThread&&) = delete;
    MultimediaThread& operator=(MultimediaThread&&) = delete;

private:
    void* registration_ = nullptr;
};

}  // namespace rwin::guest
