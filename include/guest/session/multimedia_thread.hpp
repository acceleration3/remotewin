#pragma once

namespace rwin::guest {
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

}
